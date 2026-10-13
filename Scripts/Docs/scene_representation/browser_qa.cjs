/* Run with NODE_PATH pointing to a Playwright installation and optional ARDA_BROWSER.
   Serve the repo root on port 8765 first. Screenshots go to the system temp folder. */
const {chromium}=require('playwright');
const fs=require('node:fs');
const path=require('node:path');
const os=require('node:os');
const assert=require('node:assert/strict');
(async()=>{
  const browser=await chromium.launch({headless:true,...(process.env.ARDA_BROWSER?{executablePath:process.env.ARDA_BROWSER}:{})});
  const out=path.join(os.tmpdir(),'arda-scene-research'); fs.mkdirSync(out,{recursive:true});
  const context=await browser.newContext({viewport:{width:1440,height:1000},reducedMotion:'reduce'});
  await context.route('**/*',route=>route.request().url().startsWith('http://127.0.0.1:8765/')?route.continue():route.abort());
  const page=await context.newPage(); const errors=[]; page.on('pageerror',e=>errors.push(e.message));
  const base='http://127.0.0.1:8765/Docs/Unreal/scene-representation.html';
  const response=await page.goto(base); assert.equal(response.status(),200);
  await page.locator('.chapter').last().waitFor();
  assert.equal(await page.locator('.chapter').count(),15);
  for(const width of [1440,768,390,640]){
    await page.setViewportSize({width,height:1000});
    await page.goto(base);
    await page.evaluate(()=>window.scrollTo(0,0));
    const dims=await page.evaluate(()=>({width:innerWidth,scroll:document.documentElement.scrollWidth}));
    if(dims.scroll>dims.width+1) console.log(await page.evaluate(()=>[...document.querySelectorAll('body *')].filter(e=>e.getBoundingClientRect().right>innerWidth+1&&!e.closest('.diagram-scroll,.table-wrap,pre')).map(e=>({tag:e.tagName,cls:e.className,id:e.id,width:e.getBoundingClientRect().width})).slice(0,12)));
    assert(dims.scroll<=dims.width+1,`Page overflows at ${width}: ${JSON.stringify(dims)}`);
    const missing=await page.locator('img').evaluateAll(imgs=>imgs.filter(i=>i.complete&&!i.naturalWidth).map(i=>i.src));
    assert.deepEqual(missing,[]);
    await page.screenshot({path:path.join(out,`scene-${width}.png`)});
  }
  await page.setViewportSize({width:1440,height:1000});
  await page.goto(base);
  await page.keyboard.press('Tab');
  assert.equal(await page.evaluate(()=>document.activeElement.textContent.trim()),'Skip to study');
  await page.keyboard.press('Enter'); assert.equal(new URL(page.url()).hash,'#main-content');
  await page.locator('#chapter-search').fill('skeletal');
  const count=await page.locator('.study-toc li:not([hidden]) a[data-chapter]').count();
  assert(count>0&&count<15); assert.equal(await page.locator('.chapter:visible').count(),15);
  await page.locator('#chapter-search').fill('not-a-real-concept-123');
  assert.equal(await page.locator('.study-toc li:not([hidden]) a[data-chapter]').count(),0);
  await page.locator('#chapter-search').fill('');
  await page.setViewportSize({width:390,height:844}); await page.goto(base);
  assert.equal(await page.locator('.study-toc details').getAttribute('open'),null);
  await page.locator('[data-nav-toggle]').click();
  assert.equal(await page.locator('[data-nav-toggle]').getAttribute('aria-expanded'),'true');
  assert(await page.locator('#study-primary-nav a[href="../glossary.html"]').isVisible());
  await page.setViewportSize({width:1440,height:1000});
  await page.goto(base+'#src-shade-skeletal-cards-default');
  assert(await page.locator('#src-shade-skeletal-cards-default').isVisible());
  await page.goto(base+'#gpu-scene');
  await page.screenshot({path:path.join(out,'scene-gpu.png')});
  const term=page.locator('a[data-term="resource"]').first();
  await term.focus();
  assert(await term.getAttribute('aria-describedby'),'Glossary focus description missing');
  await page.keyboard.press('Escape');
  await page.locator('[data-theme-toggle]').click();
  assert.equal(await page.locator('html').getAttribute('data-theme'),'dark');
  await page.goto(base+'#materials'); await page.screenshot({path:path.join(out,'scene-dark.png')});
  await page.locator('[data-theme-toggle]').click();
  const diagrams=await page.locator('.diagram img').evaluateAll(imgs=>imgs.map(i=>i.src));
  const svgpage=await context.newPage();
  for(const src of diagrams){
    await svgpage.goto(src);
    const outside=await svgpage.evaluate(()=>{const root=document.documentElement;const vb=root.viewBox.baseVal;return [...root.querySelectorAll('text')].filter(t=>{const r=t.getBBox();return r.x<0||r.y<0||r.x+r.width>vb.width||r.y+r.height>vb.height}).map(t=>t.textContent)});
    assert.deepEqual(outside,[],`SVG labels outside viewBox: ${src}`);
    if(src.includes('three-hierarchies')||src.includes('deformation-history')) await svgpage.screenshot({path:path.join(out,path.basename(new URL(src).pathname)+'.png')});
  }
  await svgpage.close();
  assert.deepEqual(errors,[]);
  await context.close();
  const nojs=await browser.newContext({javaScriptEnabled:false,viewport:{width:390,height:844}});
  const staticPage=await nojs.newPage(); await staticPage.goto(base);
  assert.equal(await staticPage.locator('.chapter').count(),15);
  assert(await staticPage.locator('#study-primary-nav a[href="../glossary.html"]').isVisible());
  assert.equal(await staticPage.locator('[data-nav-toggle]').isVisible(),false);
  assert.equal(await staticPage.locator('[data-theme-toggle]').isVisible(),false);
  assert(await staticPage.locator('a[href="#lumen"]').first().isVisible());
  await staticPage.goto(base+'#src-arch-scene'); assert(await staticPage.locator('#src-arch-scene').isVisible());
  await browser.close();
  console.log('PASS: non-root hosting, offline assets, 1440/768/390/640 widths, keyboard skip, search, evidence deep link, glossary focus, no-JS reading. Screenshots: '+out);
})().catch(e=>{console.error(e);process.exit(1)});
