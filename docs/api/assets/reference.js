const input = document.querySelector('#filter');
const status = document.querySelector('#search-status');
const switches = document.querySelector('#language-switch');
const localized = Array.from(document.querySelectorAll('[data-zh][data-en]'));
const searchable = Array.from(document.querySelectorAll('[data-search]'));
let language = 'zh';
const storageKey = 'viv.api.language';
const placeholder = {
  zh: '例如：Popup、setAutoHeight、冻结、branch',
  en: 'For example: Popup, setAutoHeight, frozen, branch'
};

function applyFilter() {
  if (!input) return;
  const terms = input.value.toLocaleLowerCase().trim().split(/\s+/).filter(Boolean);
  let visible = 0;
  searchable.forEach(node => {
    // Search either language, even after changing the display language.
    const translations = Array.from(node.querySelectorAll('[data-zh][data-en]'))
      .map(part => part.dataset.zh + ' ' + part.dataset.en).join(' ');
    const text = (node.dataset.search + ' ' + node.textContent + ' ' + translations).toLocaleLowerCase();
    node.hidden = !terms.every(term => text.includes(term));
    if (!node.hidden) visible++;
  });
  if (status) status.textContent = terms.length
    ? (language === 'en' ? `${visible} matches` : `匹配 ${visible} 项`) : '';
}

function updateLinks() {
  document.querySelectorAll('a[href]').forEach(link => {
    const raw = link.getAttribute('href');
    // Keep directory prefixes when carrying language across reference pages.
    if (!/^(?:\.\.\/index|(?:pages\/)?[a-z][a-z0-9_-]*)\.html(?:[?#]|$)/i.test(raw)) return;
    const url = new URL(raw, window.location.href);
    url.searchParams.set('lang', language);
    link.setAttribute('href', raw.split(/[?#]/, 1)[0] + url.search + url.hash);
  });
}

function setLanguage(next, remember = true) {
  language = next === 'en' ? 'en' : 'zh';
  document.documentElement.lang = language === 'en' ? 'en' : 'zh-CN';
  localized.forEach(node => { node.textContent = node.dataset[language]; });
  if (input) input.placeholder = placeholder[language];
  document.title = document.querySelector('h1').textContent + ' · VirtualItemViews';
  if (switches) switches.querySelectorAll('[data-language]').forEach(button => {
    button.setAttribute('aria-pressed', String(button.dataset.language === language));
  });
  if (remember) {
    try { localStorage.setItem(storageKey, language); } catch (_) { /* file/private mode */ }
  }
  const url = new URL(window.location.href);
  url.searchParams.set('lang', language);
  try { window.history.replaceState(null, '', url.href); } catch (_) { /* local-file restrictions */ }
  updateLinks();
  applyFilter();
}

function revealTarget(hash) {
  let id;
  try { id = decodeURIComponent(hash.slice(1)); } catch (_) { return; }
  const target = document.getElementById(id);
  if (!target) return;
  if (input) input.value = '';
  applyFilter();
  for (let parent = target.parentElement; parent; parent = parent.parentElement) {
    if (parent.tagName === 'DETAILS') parent.open = true;
  }
  target.scrollIntoView({block: 'start'});
  target.focus({preventScroll: true});
}

if (input) input.addEventListener('input', applyFilter);
if (switches) {
  switches.hidden = false;
  switches.querySelectorAll('[data-language]').forEach(button => {
    button.addEventListener('click', () => setLanguage(button.dataset.language));
  });
}
document.querySelectorAll('a[href^="#"]').forEach(link => {
  link.addEventListener('click', () => revealTarget(link.getAttribute('href')));
});
window.addEventListener('hashchange', () => revealTarget(window.location.hash));
let preferred = new URL(window.location.href).searchParams.get('lang');
if (preferred !== 'zh' && preferred !== 'en') {
  try { preferred = localStorage.getItem(storageKey); } catch (_) { /* file/private mode */ }
}
setLanguage(preferred, false);
if (window.location.hash) requestAnimationFrame(() => revealTarget(window.location.hash));
