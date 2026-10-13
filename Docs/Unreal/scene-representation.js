/* Optional reading aids; chapter content and links work without JavaScript. */
(() => {
  'use strict';
  document.documentElement.classList.add('study-ready');
  const chapters = [...document.querySelectorAll('.study > .chapter')];
  const links = [...document.querySelectorAll('.study-toc a[data-chapter]')];
  const search = document.getElementById('chapter-search');
  const status = document.getElementById('chapter-search-status');
  const fill = document.getElementById('reading-progress');
  const toc = document.querySelector('.study-toc details');
  const narrow = window.matchMedia('(max-width: 800px)');
  if (narrow.matches) toc.open = false;
  narrow.addEventListener('change', event => { toc.open = !event.matches; });
  search.addEventListener('input', () => {
    const query = search.value.toLowerCase().trim();
    let count = 0;
    links.forEach(link => {
      const section = document.getElementById(link.dataset.chapter);
      const match = !query || section.textContent.toLowerCase().includes(query);
      link.parentElement.hidden = !match;
      count += Number(match);
    });
    document.querySelectorAll('.toc-group').forEach(group => { group.hidden = Boolean(query); });
    status.textContent = query ? `${count} chapters contain “${search.value.trim()}”. Article text stays visible.` : 'Search finds chapters; use Ctrl+F to search within the page.';
  });
  function openTarget() {
    let id;
    try { id = decodeURIComponent(location.hash.slice(1)); } catch (_) { return; }
    const target = document.getElementById(id);
    if (!target) return;
    let node = target.parentElement;
    while (node) { if (node.tagName === 'DETAILS') node.open = true; node = node.parentElement; }
    if (id.startsWith('src-')) target.scrollIntoView({block:'start'});
  }
  window.addEventListener('hashchange', openTarget);
  openTarget();
  function updateProgress() {
    const y = window.scrollY + Math.min(300, window.innerHeight * 0.35);
    let active = chapters[0];
    chapters.forEach(ch => { if (ch.offsetTop <= y) active = ch; });
    links.forEach(link => { if (active && link.dataset.chapter === active.id) link.setAttribute('aria-current','location'); else link.removeAttribute('aria-current'); });
    const article = document.querySelector('.study');
    const total = article.offsetHeight - window.innerHeight;
    fill.style.width = `${Math.max(0,Math.min(100,(window.scrollY - article.offsetTop)/Math.max(total,1)*100))}%`;
  }
  let scheduled = false;
  window.addEventListener('scroll', () => { if (!scheduled) { scheduled = true; requestAnimationFrame(() => { scheduled = false; updateProgress(); }); } }, {passive:true});
  window.addEventListener('resize', updateProgress);
  updateProgress();
})();
