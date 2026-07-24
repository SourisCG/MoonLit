import './app.css';
import { mount } from 'svelte';
import App from './App.svelte';

const target = document.getElementById('app');

if (!target) {
  throw new Error('No existe el elemento #app');
}

try {
  mount(App, {
    target,
    intro: false,
  });
} catch (error) {
  console.error('MoonLit no pudo iniciar la interfaz', error);
  target.replaceChildren();

  const message = document.createElement('div');
  message.style.cssText =
    'display:grid;min-height:100vh;place-content:center;padding:32px;background:#0d1117;color:#f3f5f7;font:14px system-ui,sans-serif;text-align:center';
  message.textContent = `MoonLit no pudo iniciar la interfaz: ${String(error)}`;
  target.append(message);
}
