// SPDX-License-Identifier: MIT
// Raster copies of the flat overlays are review artifacts, never PDF inputs.
'use strict';
const fs=require('node:fs');
const path=require('node:path');
const {execFileSync}=require('node:child_process');
async function main() {
    const {chromium}=require(process.env.PLAYWRIGHT_MODULE || 'playwright');
    const output=path.resolve(process.argv[2]);
    const options={headless:true};
    if(process.env.CHROMIUM_EXECUTABLE) options.executablePath=process.env.CHROMIUM_EXECUTABLE;
    else if(!fs.existsSync(chromium.executablePath()) && process.platform==='darwin') options.executablePath='/Applications/Google Chrome.app/Contents/MacOS/Google Chrome';
    const browser=await chromium.launch(options);
    try {
        const page=await browser.newPage({viewport:{width:1400,height:1000},deviceScaleFactor:1});
        for(const filename of fs.readdirSync(output).filter(name=>name.endsWith('.svg'))) {
            await page.goto('file://'+path.join(output,filename));
            await page.screenshot({path:path.join(output,filename.replace('.svg','-overlay.png'))});
        }
        if(process.env.MANUAL_GUIDE_REFERENCE) {
            // SVG navigation leaves an XML document; setContent requires an HTML document.
            await page.goto('about:blank');
            // Both documents are rasterized to the same A4 scale; source pictures stay intact.
            for(const language of ['en','ru']) {
                const suffix=language==='en'?'':'.ru';
                execFileSync('pdftoppm',['-f','1','-l','12','-scale-to','1400','-png',
                    path.join(process.env.MANUAL_GUIDE_REFERENCE,`perinkpheral-app-guide${suffix}.pdf`),
                    path.join(output,`guide-${language}`)]);
                for(const [name,manualPage,guidePage] of [['cover',1,1],['contents',2,2],['section',3,5],['tables',6,6]]) {
                    const findPage=(prefix,number)=>fs.readdirSync(output).find(filename=>
                        new RegExp(`^${prefix}-0*${number}\\.png$`).test(filename));
                    const files=[findPage(`guide-${language}`,guidePage),findPage(`manual-${language}`,manualPage)];
                    if(files.some(filename=>!filename)) throw Error('Missing pages for comparison: '+name);
                    const images=files.map(filename=>'data:image/png;base64,'+fs.readFileSync(path.join(output,filename)).toString('base64'));
                    await page.setContent(`<!doctype html><html><body style="margin:0;display:flex;width:max-content">${images.map(src=>`<img src="${src}" style="display:block">`).join('')}</body></html>`);
                    await page.locator('img').evaluateAll(images=>Promise.all(images.map(image=>image.decode())));
                    await page.screenshot({path:path.join(output,`compare-${language}-${name}.png`),fullPage:true});
                }
            }
        }
    } finally {await browser.close();}
}
main().catch(error=>{console.error(error);process.exitCode=1;});
