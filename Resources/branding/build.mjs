// Rebuild: node Resources/branding/build.mjs /absolute/path/to/@resvg/resvg-js
import { createRequire } from 'node:module';
import { mkdirSync, writeFileSync, readFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { execFileSync } from 'node:child_process';
const root = dirname(fileURLToPath(import.meta.url));
const require = createRequire(import.meta.url);
const { Resvg } = require(process.argv[2] || '@resvg/resvg-js');
for (const dir of ['svg', 'png', 'icons', 'icons/xerum.iconset']) mkdirSync(join(root, dir), { recursive: true });

const defs = `<defs>
  <linearGradient id="back" x1="356" y1="780" x2="920" y2="208" gradientUnits="userSpaceOnUse"><stop stop-color="#03e4e8"/><stop offset=".24" stop-color="#087fbd"/><stop offset=".52" stop-color="#4433b9"/><stop offset=".77" stop-color="#6932ee"/><stop offset="1" stop-color="#dba6ff"/></linearGradient>
  <linearGradient id="front" x1="338" y1="215" x2="902" y2="779" gradientUnits="userSpaceOnUse"><stop stop-color="#29f6f6"/><stop offset=".3" stop-color="#48ace9"/><stop offset=".49" stop-color="#89dcff"/><stop offset=".57" stop-color="#eab0ff"/><stop offset=".7" stop-color="#7328ea"/><stop offset="1" stop-color="#ec4eff"/></linearGradient>
  <linearGradient id="rimBack" x1="350" y1="780" x2="915" y2="200" gradientUnits="userSpaceOnUse"><stop stop-color="#68ffff"/><stop offset=".45" stop-color="#7266ff"/><stop offset=".85" stop-color="#faf5ff"/><stop offset="1" stop-color="#ee9dff"/></linearGradient>
  <linearGradient id="rimFront" x1="335" y1="200" x2="905" y2="790" gradientUnits="userSpaceOnUse"><stop stop-color="#a5ffff"/><stop offset=".43" stop-color="#91ebff"/><stop offset=".56" stop-color="#fffaff"/><stop offset=".76" stop-color="#d361ff"/><stop offset="1" stop-color="#ffbdff"/></linearGradient>
  <linearGradient id="sheen" x1="510" y1="365" x2="670" y2="555" gradientUnits="userSpaceOnUse"><stop stop-color="white" stop-opacity=".04"/><stop offset=".6" stop-color="white" stop-opacity=".6"/><stop offset="1" stop-color="white" stop-opacity="0"/></linearGradient>
  <linearGradient id="backSheen" x1="810" y1="233" x2="689" y2="387" gradientUnits="userSpaceOnUse"><stop stop-color="white" stop-opacity=".78"/><stop offset="1" stop-color="white" stop-opacity="0"/></linearGradient>
  <radialGradient id="cross"><stop stop-color="#ffffff" stop-opacity=".95"/><stop offset=".5" stop-color="#f3d9ff" stop-opacity=".62"/><stop offset="1" stop-color="#f0c2ff" stop-opacity="0"/></radialGradient>
  <radialGradient id="ambient"><stop stop-color="#122747"/><stop offset=".65" stop-color="#0c1024"/><stop offset="1" stop-color="#070710"/></radialGradient>
  <linearGradient id="letters" x1="0" y1="0" x2="0" y2="1"><stop stop-color="#f5efff"/><stop offset=".53" stop-color="#c3e5fa"/><stop offset="1" stop-color="#edf8ff"/></linearGradient>
  <filter id="glow" x="-60%" y="-60%" width="220%" height="220%" color-interpolation-filters="sRGB"><feGaussianBlur stdDeviation="25"/></filter>
  <filter id="soft" x="-30%" y="-30%" width="160%" height="160%" color-interpolation-filters="sRGB"><feGaussianBlur stdDeviation="7"/></filter>
  <clipPath id="frontClip"><path d="M417.7 215 L890.7 699 A50 50 0 0 1 819.3 769 L346.3 285 A50 50 0 0 1 417.7 215 Z"/></clipPath>
</defs>`;
const bars = (rim = true) => `<path d="M395 734 L873 250" fill="none" stroke="${rim ? 'url(#rimBack)' : 'url(#back)'}" stroke-width="104" stroke-linecap="round"/><path d="M395 734 L873 250" fill="none" stroke="url(#back)" stroke-width="98" stroke-linecap="round"/><path d="M382 250 L855 734" fill="none" stroke="${rim ? 'url(#rimFront)' : 'url(#front)'}" stroke-width="104" stroke-linecap="round"/><path d="M382 250 L855 734" fill="none" stroke="url(#front)" stroke-width="98" stroke-linecap="round"/>`;
const faithful = `<g opacity=".65" filter="url(#glow)">${bars(false)}</g><path d="M395 734 L873 250" fill="none" stroke="url(#rimBack)" stroke-width="104" stroke-linecap="round"/><path d="M395 734 L873 250" fill="none" stroke="url(#back)" stroke-width="98" stroke-linecap="round"/><path d="M623 431 L837 218 Q854 201 874 202 L802 310 L691 467 Z" fill="url(#backSheen)"/><path d="M382 250 L855 734" fill="none" stroke="url(#rimFront)" stroke-width="104" stroke-linecap="round"/><path d="M382 250 L855 734" fill="none" stroke="url(#front)" stroke-width="98" stroke-linecap="round"/><g clip-path="url(#frontClip)"><path d="M358 194 Q424 215 354 298 L536 473 Q590 541 692 566 L627 432 L546 338 L442 221 Z" fill="url(#sheen)"/><ellipse cx="629" cy="498" rx="89" ry="110" transform="rotate(-44 629 498)" fill="url(#cross)"/></g>`;

// Custom geometric outlines; all lettering remains vector geometry.
const wordmark = `<g fill="url(#letters)" fill-rule="evenodd">
<path d="M188 869 H207 L242 902 L278 869 H297 L253 912 L297 956 H278 L242 922 L207 956 H188 L231 912 Z"/>
<path d="M392 869 H481 V882 H392 Z M392 906 H478 V919 H392 Z M392 943 H481 V956 H392 Z"/>
<path d="M580 956 V869 H637 Q672 869 672 897 Q672 917 650 924 L675 956 H657 L633 926 H594 V956 Z M594 882 V913 H635 Q658 913 658 897 Q658 882 636 882 Z"/>
<path d="M770 869 H784 V922 Q784 943 817 943 Q851 943 851 922 V869 H865 V923 Q865 957 817 957 Q770 957 770 923 Z"/>
<path d="M962 956 V869 H980 L1015 911 L1049 869 H1067 V956 H1053 V888 L1015 934 L976 888 V956 Z"/>
</g>`;
const glyphs = {
 W:'M0 0 L5 18 L10 5 L15 18 L20 0', A:'M0 18 L8 0 L16 18 M3 12 H13', V:'M0 0 L8 18 L16 0',
 E:'M15 0 H0 V18 H15 M0 9 H12', F:'M15 0 H0 V18 M0 9 H12', O:'M8 0 C-3 0 -3 18 8 18 C19 18 19 0 8 0 Z',
 R:'M0 18 V0 H8 C18 0 18 9 8 9 H0 M8 9 L17 18', M:'M0 18 V0 L9 11 L18 0 V18',
 S:'M16 2 C-4 -6 -5 10 8 9 C22 8 19 25 0 16', Y:'M0 0 L8 9 L16 0 M8 9 V18',
 N:'M0 18 V0 L16 18 V0', T:'M0 0 H18 M9 0 V18', H:'M0 0 V18 M16 0 V18 M0 9 H16',
 I:'M4 0 V18', Z:'M0 0 H16 L0 18 H16'
};
const tagline = `<g fill="none" stroke="#7185ad" stroke-width="2.4" stroke-linecap="square" stroke-linejoin="round">${[...'WAVEFORM SYNTHESIZER'].map((c,i)=>c===' '? '' : `<path transform="translate(${228+i*43.5} 1005)" d="${glyphs[c]}"/>`).join('')}</g>`;
function svg(content, title, viewBox='0 0 1254 1254', extraDefs=defs) {
 return `<?xml version="1.0" encoding="UTF-8"?>\n<svg xmlns="http://www.w3.org/2000/svg" viewBox="${viewBox}" role="img" aria-label="${title}"><title>${title}</title>${extraDefs}${content}</svg>\n`;
}
const bg = '<rect width="1254" height="1254" fill="url(#ambient)"/>';
const mark = content => `<g transform="translate(-313 -111) scale(1.5)">${content}</g>`;
const files = {
 'xerum-logo-dark': svg(bg+faithful+`<g opacity=".35" filter="url(#soft)">${wordmark}</g>`+wordmark+tagline,'Xerum — Waveform Synthesizer'),
 'xerum-logo-transparent': svg(faithful+wordmark+tagline,'Xerum — Waveform Synthesizer'),
 'xerum-mark-dark': svg(bg+mark(faithful),'Xerum symbol'),
 'xerum-mark-transparent': svg(mark(faithful),'Xerum symbol'),
 'xerum-mark-simple-dark': svg('<rect width="1254" height="1254" fill="#0b0e20"/>'+mark(bars(false)),'Xerum small icon'),
 'xerum-mark-simple': svg(mark(bars(false)),'Xerum simplified symbol'),
 'xerum-mark-mono-light': svg(mark('<path d="M395 734 L873 250 M382 250 L855 734" fill="none" stroke="#ffffff" stroke-width="104" stroke-linecap="round"/>'),'Xerum white symbol','0 0 1254 1254',''),
 'xerum-mark-mono-dark': svg(mark('<path d="M395 734 L873 250 M382 250 L855 734" fill="none" stroke="#0b0e20" stroke-width="104" stroke-linecap="round"/>'),'Xerum dark symbol','0 0 1254 1254','')
};
const render = (source, size) => new Resvg(source, {fitTo:{mode:'width', value:size}}).render().asPng();
for (const [name,source] of Object.entries(files)) {
 writeFileSync(join(root,'svg',name+'.svg'),source);
 writeFileSync(join(root,'png',name+'.png'),render(source,1254));
}
const sizes=[16,32,48,64,128,256,512,1024];
for (const size of sizes) writeFileSync(join(root,'icons',`xerum-${size}.png`),render(files[size<=48?'xerum-mark-simple-dark':'xerum-mark-dark'],size));
for (const base of [16,32,128,256,512]) for (const scale of [1,2]) {
 const size=base*scale;
 writeFileSync(join(root,'icons/xerum.iconset',`icon_${base}x${base}${scale===2?'@2x':''}.png`),render(files[base<=32?'xerum-mark-simple-dark':'xerum-mark-dark'],size));
}
// ICO directory with PNG-compressed frames, supported by modern Windows.
const icoSizes=[16,32,48,64,128,256];
const frames=icoSizes.map(s=>readFileSync(join(root,'icons',`xerum-${s}.png`)));
const header=Buffer.alloc(6+16*frames.length); header.writeUInt16LE(1,2); header.writeUInt16LE(frames.length,4);
let offset=header.length;
frames.forEach((frame,i)=>{const p=6+16*i,s=icoSizes[i]; header[p]=s===256?0:s; header[p+1]=s===256?0:s; header.writeUInt16LE(1,p+4);header.writeUInt16LE(32,p+6);header.writeUInt32LE(frame.length,p+8);header.writeUInt32LE(offset,p+12);offset+=frame.length;});
writeFileSync(join(root,'icons/xerum.ico'),Buffer.concat([header,...frames]));
if(process.platform==='darwin' && !process.argv.includes('--skip-icns')) execFileSync('/usr/bin/iconutil',['-c','icns',join(root,'icons/xerum.iconset'),'-o',join(root,'icons/xerum.icns')]);

const previewNames=Object.keys(files);
const tiles=previewNames.map((name,i)=>{
 const x=32+(i%4)*310,y=72+Math.floor(i/4)*355;
 // Prefix IDs so each embedded vector remains independently addressable.
 const body=files[name].replace(/<\?xml[^>]+>/,'').replace(/id="([^"]+)"/g,`id="p${i}-$1"`).replace(/url\(#([^)]+)\)/g,`url(#p${i}-$1)`);
 return `<rect x="${x}" y="${y}" width="286" height="286" rx="12" fill="${name.includes('mono-dark')?'#edf2fa':'#182139'}"/><svg x="${x}" y="${y}" width="286" height="286" viewBox="0 0 1254 1254">${body.replace(/<svg[^>]*>/,'').replace(/<\/svg>\s*$/,'')}</svg><text x="${x}" y="${y+311}" fill="#c4cee3" font-family="sans-serif" font-size="13">${name}</text>`;
}).join('');
const preview=svg('<rect width="1304" height="815" fill="#090d17"/><text x="32" y="40" fill="white" font-family="sans-serif" font-size="22">XERUM / Brand assets</text>'+tiles,'Xerum asset overview','0 0 1304 815','');
writeFileSync(join(root,'preview.svg'),preview);
writeFileSync(join(root,'preview.png'),render(preview,1956));
console.log(`Generated ${Object.keys(files).length} SVG masters, PNG previews, app icons and contact sheet.`);
