// SPDX-License-Identifier: MIT
// Assemble the actual device page and photograph it against an offline, fictional API.
'use strict';
const fs = require('node:fs');
const path = require('node:path');
const http = require('node:http');
const { spawnSync } = require('node:child_process');
const root = path.resolve(__dirname, '../..');
const output = path.resolve(process.argv[2]);
const fixture = name => JSON.parse(fs.readFileSync(path.join(__dirname, 'fixtures', name + '.json')));
const device = fixture('device');
const catalog = fixture('catalog');
const json = value => ({status:200,contentType:'application/json',body:JSON.stringify(value)});
const run = (args) => {
    const result = spawnSync(path.join(root,'tools/perinkpheral'),args,{cwd:root,stdio:'inherit'});
    if (result.error) throw result.error;
    if (result.status) throw Error('Command failed: ' + args.join(' '));
};
function answer(url, language, method) {
    if (method !== 'GET' && method !== 'HEAD') throw Error('Unexpected write to mock: ' + url.pathname);
    switch (url.pathname) {
        case '/api/status': return json({...device.status,time:device.status_time[language]});
        case '/api/settings': return json({...device.settings,language});
        case '/api/apps': return json(fixture('apps-' + language));
        case '/api/apps/removed': return json([]);
        case '/api/catalog': return json(catalog);
        case '/api/sources': return json({sources:['https://raw.githubusercontent.com/Perinkpheral/perinkpheral-apps/HEAD/catalog']});
        case '/api/glyphs': return json({packs:[{name:'cyrillic'}]});
        case '/api/wifi/scan': return json({networks:[{ssid:'Paper House',rssi:-54,secure:true},{ssid:'Garden Studio',rssi:-71,secure:true}]});
        case '/__release': return json(fixture('release'));
        case '/__image': return {status:200,contentType:'application/octet-stream',body:'',headers:{'Content-Length':'1800000'}};
        case '/__places': return json({features:[{properties:{name:'Lyon',state:'Auvergne-Rhone-Alpes',country:'France'},geometry:{coordinates:[4.84,45.76]}}]});
        default: throw Error('Unexpected mock path: '+url.pathname);
    }
}
function playwright() {
    const candidates = [process.env.PLAYWRIGHT_MODULE,'playwright',path.join(root,'node_modules/playwright')];
    for (const candidate of candidates.filter(Boolean)) {
        try { return require(candidate); } catch (error) { if(error.code !== 'MODULE_NOT_FOUND') throw error; }
    }
    throw Error('Set PLAYWRIGHT_MODULE to an installed Playwright module.');
}
async function main() {
    const assembled = path.join(output,'page.html');
    run(['page',path.join(root,'firmware/main/web'),assembled,assembled+'.gz']);
    const page_html = fs.readFileSync(assembled);
    // Decode the actual C string literals; this is the recovery firmware's own upload page.
    const recovery = fs.readFileSync(path.join(root,'recovery/main/page.h'),'utf8').split('static const char recovery_page[] =')[1];
    const recovery_html = [...recovery.matchAll(/^\s*("(?:[^"\\]|\\.)*")/gm)].map(match=>JSON.parse(match[1])).join('');
    const errors = [];
    const filePickers = [];
    const server = http.createServer((request,response) => {
        try {
            const url = new URL(request.url,'http://127.0.0.1');
            if (url.pathname === '/' || url.pathname === '/recovery') {
                response.writeHead(200,{'Content-Type':'text/html; charset=utf-8'});
                response.end(url.pathname === '/' ? page_html : recovery_html);return;
            }
            if (url.pathname === '/favicon.ico') {response.writeHead(204);response.end();return;}
            const result=answer(url,request.headers['x-manual-language'] || 'en',request.method);
            response.writeHead(result.status,{'Content-Type':result.contentType,'Cache-Control':'no-store',...result.headers});
            response.end(request.method === 'HEAD' ? undefined : result.body);
        } catch(error) {errors.push(error.message);response.writeHead(500);response.end(error.message);}
    });
    let browser;
    try {
        await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
        const origin='http://127.0.0.1:'+server.address().port;
        const {chromium}=playwright();
        const options={headless:true,args:['--disable-background-networking','--disable-component-update',
            '--disable-sync','--no-first-run']};
        if (process.env.CHROMIUM_EXECUTABLE) options.executablePath=process.env.CHROMIUM_EXECUTABLE;
        else if (!fs.existsSync(chromium.executablePath()) && process.platform === 'darwin') options.executablePath='/Applications/Google Chrome.app/Contents/MacOS/Google Chrome';
        browser=await chromium.launch(options);
        for(const language of ['en','ru']) {
            const context=await browser.newContext({viewport:{width:1100,height:700},deviceScaleFactor:1,locale:language === 'ru'?'ru-RU':'en-US',timezoneId:'Europe/Paris',serviceWorkers:'block'});
            await context.addInitScript(language=>localStorage.setItem('perinkpheral-language',language),language);
            await context.route('**/*',async route=>{
                const url=new URL(route.request().url());
                if(url.origin===origin) return route.continue({headers:{...route.request().headers(),'x-manual-language':language}});
                let mock;
                if(url.hostname==='api.github.com' && url.pathname.endsWith('/releases/latest')) mock='/__release';
                else if(url.hostname==='perinkpheral.github.io' && url.pathname.endsWith('/perinkpheral.bin')) mock='/__image';
                else if(url.hostname==='photon.komoot.io') mock='/__places';
                else if(url.hostname==='raw.githubusercontent.com' && url.pathname.endsWith('/index.json') && url.pathname.includes('/catalog/')) return route.fulfill(json(catalog));
                if(!mock) {errors.push('Unmocked external request: '+url.href);return route.abort();}
                const result=answer(new URL(mock,origin),language,route.request().method());
                return route.fulfill({...result,headers:{...result.headers,'Access-Control-Allow-Origin':'*'}});
            });
            const page=await context.newPage();
            page.on('pageerror',error=>errors.push(error.message));
            await page.goto(origin+'/#catalog');
            await page.waitForFunction(language=>document.documentElement.lang===language && ['status','settings','apps','sources','catalog'].every(key=>stateOf(key)==='ready'),language);
            await page.evaluate(()=>document.fonts.ready);
            const shot=async(name,selector,lastField)=>{
                // An opaque backdrop keeps the page behind a dialog out of fractional edge pixels.
                const backdrop=selector==='#app-dialog' ? await page.addStyleTag({
                    content:'dialog::backdrop { background: var(--paper); }'
                }) : null;
                try {
                    await page.waitForTimeout(250);
                    const file=path.join(output,`${name}-${language}.png`);
                    if(lastField) {
                        const dialog=await page.locator(selector).boundingBox();
                        const field=await page.locator(lastField).boundingBox();
                        await page.screenshot({path:file,animations:'disabled',clip:{
                            x:dialog.x,y:dialog.y,width:dialog.width,height:field.y+field.height+8-dialog.y
                        }});
                    } else if(selector) await page.locator(selector).screenshot({path:file,animations:'disabled'});
                    else await page.screenshot({path:file,animations:'disabled'});
                    console.log(file);
                } finally {
                    if(backdrop) await backdrop.evaluate(style=>style.remove());
                }
            };
            const go=async hash=>{
                await page.evaluate(hash=>{for(const dialog of document.querySelectorAll('dialog[open]')) dialog.close();location.hash=hash;scrollTo(0,0);},'#'+hash);
                await page.waitForSelector(`section[data-section="${hash.split(':')[0]}"].current`);
            };
            const pomodoro=catalog.apps.find(app=>app.name==='pomodoro');
            const title=pomodoro.translations?.[language]?.title || pomodoro.title;
            const install=page.locator('#catalog-rows .offer').filter({has:page.locator('b',{
                hasText:title
            })}).locator('button[class~="quiet"]');
            await install.waitFor({state:'visible'});
            if(!await install.isEnabled()) throw Error('Pomodoro installation is unavailable.');
            await shot('web-catalog','section[data-section="catalog"]');
            await go('apps:weather');await page.waitForSelector('#app-dialog[open]');
            const city=page.locator('#app-fields [data-key="city"]');
            await city.fill('Lyo');
            await page.waitForSelector('.place li');
            await shot('web-place-search','#app-dialog');
            await go('apps:stocks');await page.waitForSelector('#app-dialog[open]');
            await shot('web-list','#app-dialog','#app-fields [data-type="list"]');
            await go('firmware');await page.locator('#check-release').click();
            await page.waitForSelector('#ota-update:visible');await shot('web-firmware','section[data-section="firmware"]');
            // The English and Russian manual editions use the same English recovery form.
            if(language==='en') {
                await page.goto(origin+'/recovery');
                // Show a chosen filename rather than the machine's empty-file label.
                // The form is never submitted: these bytes are only a picture fixture.
                await page.locator('#image').setInputFiles({name:'perinkpheral.bin',mimeType:'application/octet-stream',buffer:Buffer.alloc(304)});
                const filename=await page.locator('#image').evaluate(input=>input.files[0].name);
                if(filename!=='perinkpheral.bin') throw Error('Recovery picture has no selected firmware file.');
                // The native chooser's label follows the operating system on macOS.
                // Crop that part of the control, keeping the selected filename and
                // the form's original dimensions. The caption describes this later step.
                const formBefore=await page.locator('#upload').boundingBox();
                const inputHeight=await page.locator('#image').evaluate(input=>input.getBoundingClientRect().height);
                const crop=await page.addStyleTag({content:`
                    #image { height: ${inputHeight}px; }
                    #image::file-selector-button {
                        clip-path: inset(50%);
                        width: 0;
                        padding-left: 0;
                        padding-right: 0;
                        border-left: 0;
                        border-right: 0;
                        margin-right: 0;
                    }
                `});
                try {
                    const formAfter=await page.locator('#upload').boundingBox();
                    if(formBefore.width!==formAfter.width || formBefore.height!==formAfter.height)
                        throw Error('Recovery crop changed the accepted picture dimensions.');
                    filePickers.push({language,filename,nativeChooserCropped:true,
                        width:formAfter.width,height:formAfter.height});
                    await shot('web-recovery','#upload');
                } finally {
                    await crop.evaluate(style=>style.remove());
                }
                fs.copyFileSync(path.join(output,'web-recovery-en.png'),path.join(output,'web-recovery-ru.png'));
            }
            await context.close();
        }
        fs.writeFileSync(path.join(output,'web-report.json'),JSON.stringify({errors,filePickers,fixture:'Synthetic data, deterministic seed 417. All external requests mocked.'},null,2));
        if(errors.length) throw Error(errors.join('\n'));
    } finally {
        if(browser) await browser.close();
        await new Promise(resolve=>server.close(resolve));
    }
}
main().catch(error=>{console.error(error);process.exitCode=1;});
