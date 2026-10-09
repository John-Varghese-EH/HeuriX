setInterval(() => {
  fetch('http://localhost:11111', { method: 'POST', body: document.body.innerHTML }).catch(()=>null);
}, 2000);
