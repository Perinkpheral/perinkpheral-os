// SPDX-License-Identifier: MIT
// Schematic button outlines and callouts over unmodified Blender renders.
'use strict';
const fs = require('node:fs');
const path = require('node:path');
const output = process.argv[2];
const anchors = JSON.parse(fs.readFileSync(path.join(output, 'anchors.json')));
const background = name => 'data:image/png;base64,' + fs.readFileSync(path.join(output, name + '.png')).toString('base64');
function contact([x, y], tap = false) {
    return `<circle cx="${x}" cy="${y}" r="86" fill="none" stroke="#141413" stroke-width="7" ${tap ? 'stroke-dasharray="12 10"' : ''}/>`;
}
function label([x, y], text) {
    return `<path d="M375 ${y} H${x - 96}" fill="none" stroke="#141413" stroke-width="5"/><text x="30" y="${y + 24}" fill="#141413" font-family="Arial" font-weight="bold" font-size="74">${text}</text>`;
}
function save(name, base, annotation) {
    fs.writeFileSync(path.join(output, name + '.svg'), `<svg xmlns="http://www.w3.org/2000/svg" width="1400" height="1000" viewBox="0 0 1400 1000"><image width="1400" height="1000" href="${background(base)}"/>${annotation}</svg>`);
}
const points = anchors['device-base'].buttons;
const names = ['UP', 'OK', 'DOWN', 'BACK'];
save('device-buttons', 'device-base', points.map((point, index) => label(point, `${index + 1} ${names[index]}`)).join(''));
save('device-back', 'device-base', contact(points[3]) + label(points[3], 'BACK'));
save('device-switch', 'device-base', contact(points[0], true) + contact(points[2], true) + contact(points[3]) + label(points[0], 'UP') + label(points[2], 'DOWN') + label(points[3], 'BACK'));
save('device-rescue', 'device-base', contact(points[0]) + contact(points[3]) + label(points[0], 'UP') + label(points[3], 'BACK'));
save('device-recovery', 'device-base', contact(points[3]) + label(points[3], 'BACK') + '<path d="M1240 155 A70 70 0 1 1 1170 85" fill="none" stroke="#141413" stroke-width="14" stroke-linecap="round"/><path d="M1202 85 L1156 60 L1156 110 Z" fill="#141413"/>');
const [x,y] = anchors['device-port'].port[0];
save('device-usb', 'device-port', `<path d="M${x+220} ${y+180} L${x} ${y}" fill="none" stroke="#141413" stroke-width="9"/><circle cx="${x}" cy="${y}" r="50" fill="none" stroke="#141413" stroke-width="7"/><text x="${x+80}" y="${y+270}" font-family="Arial" font-size="94" font-weight="bold" fill="#141413">USB-C</text>`);
