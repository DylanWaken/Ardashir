/* Serve repository at ARDA_ARTICLE_BASE (default http://127.0.0.1:8767/).
   NODE_PATH supplies Playwright; ARDA_BROWSER selects an installed browser. */
const {chromium}=require('playwright');
const assert=require('node:assert/strict');
const fs=require('node:fs');
const path=require('node:path');
const os=require('node:os');
(async()=>{
  const base=process.env.ARDA_ARTICLE_BASE||'http://127.0.0.1:8767/';
  const url=base+'Docs/Essays/arda-graph-and-inductor.html';
  const out=path.join(os.tmpdir(),'arda-graph-essay');fs.mkdirSync(out,{recursive:true});
  const browser=await chromium.launch({headless:true,...(process.env.ARDA_BROWSER?{executablePath:process.env.ARDA_BROWSER}:{})});
  const context=await browser.newContext({reducedMotion:'reduce'});
  const exceptions=[];const failed=[];
  await context.route('**/*',r=>r.request().url().startsWith(base)?r.continue():r.abort());
  const page=await context.newPage();
  page.on('pageerror',e=>exceptions.push(e.message));
  page.on('response',r=>{if(r.status()>=400)failed.push(r.url());});
  for(const width of [1440,768,390]){
    await page.setViewportSize({width,height:1000});assert.equal((await page.goto(url)).status(),200);
    await page.evaluate(()=>localStorage.removeItem('arda-essay-theme'));
    assert.equal(await page.locator('h1').count(),1);
    assert(await page.locator('.chapter').count()>=19);
    assert(await page.locator('figure').count()>=19);
    assert.equal(await page.locator('.data-table-details').count(),await page.locator('article table').count());
    assert.equal(await page.locator('.data-table-details[open]').count(),0);
    const size=await page.evaluate(()=>({width:innerWidth,full:document.documentElement.scrollWidth}));
    assert(size.full<=size.width+1,'Horizontal overflow '+JSON.stringify(size));
    await page.locator('.contents summary').click();
    const destination=page.locator('.contents a').nth(7);const hash=await destination.getAttribute('href');
    await destination.click();assert.equal(new URL(page.url()).hash,hash);
    assert.equal(await page.locator('.contents').getAttribute('open'),null);
    await page.goto(url);await page.screenshot({path:path.join(out,`article-${width}.png`)});
    await page.locator('#memory-as-ordering figure').scrollIntoViewIfNeeded();
    await page.locator('#memory-as-ordering img').evaluate(img=>img.decode());
    await page.screenshot({path:path.join(out,`memory-${width}.png`)});
    const tableDetails=page.locator('.data-table-details').first();
    assert.equal(await tableDetails.locator('table').isVisible(),false);
    await tableDetails.locator('summary').focus();await page.keyboard.press('Enter');
    assert(await tableDetails.locator('table').isVisible());
    await tableDetails.locator('.table-wrap').focus();
    assert.equal(await page.evaluate(()=>document.activeElement.getAttribute('role')),'region');
    await page.screenshot({path:path.join(out,`expanded-table-${width}.png`)});
    await tableDetails.locator('summary').click();assert.equal(await tableDetails.locator('table').isVisible(),false);
  }
  await page.setViewportSize({width:1440,height:1000});await page.goto(url);
  await page.keyboard.press('Tab');assert.equal(await page.evaluate(()=>document.activeElement.textContent),'Skip to article');
  await page.keyboard.press('Enter');assert.equal(new URL(page.url()).hash,'#article');
  await page.locator('.contents summary').click();await page.keyboard.press('Escape');
  assert.equal(await page.locator('.contents').getAttribute('open'),null);
  const term=page.locator('a[data-term]').first();await term.focus();
  assert.equal(await term.getAttribute('aria-describedby'),'term-definition');
  assert(await page.locator('#term-definition').isVisible());await page.keyboard.press('Escape');
  assert.equal(await page.locator('#term-definition').isVisible(),false);
  const source=page.locator('article a[href^="#source-"]').first();
  const sourceHash=await source.getAttribute('href');await source.click();assert.equal(new URL(page.url()).hash,sourceHash);
  await page.locator('.theme-button').click();assert.equal(await page.locator('html').getAttribute('data-theme'),'dark');
  await page.goto(url+'#executor-and-completion');await page.screenshot({path:path.join(out,'execution-dark.png')});
  const media=await page.locator('article img').evaluateAll(imgs=>imgs.map(i=>i.src));
  const svgPage=await context.newPage();
  for(const src of media){
    assert.equal((await context.request.get(src)).status(),200,src);
    if(!src.endsWith('.svg'))continue;
    await svgPage.goto(src);
    const issues=await svgPage.evaluate(()=>{
      const vb=document.documentElement.viewBox.baseVal;
      const texts=[...document.querySelectorAll('text')].map(t=>({text:t.textContent,b:t.getBBox()}));
      const outside=texts.filter(({b})=>b.x<-1||b.y<-1||b.x+b.width>vb.width+1||b.y+b.height>vb.height+1).map(t=>t.text);
      const collisions=[];
      for(let i=0;i<texts.length;i++)for(let j=i+1;j<texts.length;j++){
        const a=texts[i].b,b=texts[j].b;
        if(Math.min(a.x+a.width,b.x+b.width)-Math.max(a.x,b.x)>2&&Math.min(a.y+a.height,b.y+b.height)-Math.max(a.y,b.y)>2)collisions.push([texts[i].text,texts[j].text]);
      }
      return {outside,collisions};
    });
    assert.deepEqual(issues,{outside:[],collisions:[]},src);
  }
  await svgPage.goto(base+'Docs/Essays/assets/arda-graph/compiler-hazards.svg');
  await svgPage.screenshot({path:path.join(out,'hazards-svg.png')});
  assert.deepEqual(exceptions,[]);assert.deepEqual(failed,[]);
  const nojs=await browser.newContext({javaScriptEnabled:false,viewport:{width:390,height:844}});
  const staticPage=await nojs.newPage();await staticPage.goto(url);
  assert(await staticPage.locator('.chapter').count()>=19);assert.equal(await staticPage.locator('.theme-button').isVisible(),false);
  const staticDetails=staticPage.locator('.data-table-details').first();
  assert.equal(await staticDetails.locator('table').isVisible(),false);
  await staticDetails.locator('summary').click();assert(await staticDetails.locator('table').isVisible());
  await staticPage.locator('.contents summary').click();await staticPage.locator('.contents a').last().click();
  assert.equal(new URL(staticPage.url()).hash,'#source-notes');
  await browser.close();console.log('PASS: responsive article, keyboard navigation, source links, themes, no-JS reading, SVG bounds and text separation. Screenshots: '+out);
})().catch(e=>{console.error(e);process.exit(1);});
