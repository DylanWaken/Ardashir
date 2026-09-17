/* Personal review marks only; these do not record implementation or test status. */
(() => {
  'use strict';
  const key = 'ardashir.material-compiler.review.v1.71fe36aac5a8';
  const inputs = [...document.querySelectorAll('[data-review-task]')];
  const count = document.querySelector('#review-count');
  const status = document.querySelector('#review-storage-status');
  let selected = [];
  try {
    const stored = JSON.parse(localStorage.getItem(key) || '[]');
    if (Array.isArray(stored)) selected = stored;
  } catch (_) {
    status.textContent = 'Browser storage is unavailable. Review marks last for this page visit.';
  }
  const update = () => {
    let reviewed = 0;
    for (const input of inputs) {
      if (input.checked) reviewed++;
      input.closest('.material-task').classList.toggle('is-reviewed', input.checked);
    }
    count.value = `${reviewed} of ${inputs.length} tasks reviewed`;
  };
  const save = () => {
    try {
      localStorage.setItem(key, JSON.stringify(inputs.filter(input => input.checked).map(input => input.dataset.reviewTask)));
    } catch (_) {
      status.textContent = 'Browser storage is unavailable. Review marks last for this page visit.';
    }
    update();
  };
  for (const input of inputs) {
    input.checked = selected.includes(input.dataset.reviewTask);
    input.addEventListener('change', save);
  }
  document.querySelector('#clear-review').addEventListener('click', () => {
    for (const input of inputs) input.checked = false;
    save();
  });
  document.querySelector('#review-controls').hidden = false;
  update();

  const sections = [...document.querySelectorAll('.material-section')];
  const links = [...document.querySelectorAll('.material-nav nav a')];
  let pending = false;
  const selectChapter = () => {
    pending = false;
    let active = sections[0];
    for (const section of sections) {
      if (section.getBoundingClientRect().top <= 130) active = section;
    }
    for (const link of links) {
      if (link.hash === `#${active.id}`) link.setAttribute('aria-current', 'location');
      else link.removeAttribute('aria-current');
    }
  };
  window.addEventListener('scroll', () => {
    if (!pending) { pending = true; requestAnimationFrame(selectChapter); }
  }, { passive: true });
  selectChapter();
})();
