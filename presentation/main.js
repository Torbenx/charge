// Turns "COMMAND: <text>" lines inside speaker notes into a copyable
// command with a one-click "Copy" button. Runs once on the main deck's
// DOM; the notes plugin reads aside.notes.innerHTML live, so the speaker
// notes popup picks up the change automatically.
function decodeHtmlEntities(html) {
  const el = document.createElement('div');
  el.innerHTML = html;
  return el.textContent;
}

// Inlined (rather than put in overwrites.css) because the speaker notes
// popup is a separate document that doesn't load our stylesheet; a <style>
// tag embedded in the notes HTML travels with it and is applied normally
// when the popup sets it via innerHTML (unlike <script>, <style> isn't inert).
const COPY_BUTTON_STYLES = `
.command-box {
  position: relative;
  display: inline-block;
  padding: 8px 14px;
  margin: 10px 8px 4px 0;
  background: #2b2b2b;
  border-radius: 6px;
}
.command-box code {
  background: transparent;
  color: #f5f5f5;
  font-family: monospace;
}
.copy-command-btn {
  position: absolute;
  top: -10px;
  left: -10px;
  padding: 2px 12px;
  font-size: 0.85em;
  font-family: inherit;
  font-weight: bold;
  cursor: pointer;
  border-radius: 999px;
  border: none;
  background: #42affa;
  color: #fff;
  box-shadow: 0 1px 3px rgba(0, 0, 0, 0.3);
  transition: background-color 0.15s ease, transform 0.1s ease;
}
.copy-command-btn:hover {
  background: #1c98f7;
}
.copy-command-btn:active {
  background: #0b7fe0;
  transform: scale(0.96);
}
.copy-command-btn.copied {
  background: #6a9954;
}
`;

function buildCopyButton(command) {
  const button = document.createElement('button');
  button.type = 'button';
  button.className = 'copy-command-btn';
  button.textContent = '⧉';
  button.setAttribute(
    'onclick',
    `navigator.clipboard.writeText(${JSON.stringify(command)}).then(() => { this.classList.add('copied'); setTimeout(() => { this.classList.remove('copied'); }, 1200); })`
  );
  return button.outerHTML;
}

function enhanceCommandNotes() {
  document.querySelectorAll('aside.notes').forEach((aside) => {
    if (!/COMMAND:/i.test(aside.innerHTML)) return;

    const withButtons = aside.innerHTML.replace(
      /COMMAND:\s*([^<\n]+)/gi,
      (match, commandHtml) => {
        const command = decodeHtmlEntities(commandHtml).trim();
        return `<span class="command-box"><code>${commandHtml.trim()}</code>${buildCopyButton(command)}</span>`;
      }
    );
    aside.innerHTML = `<style>${COPY_BUTTON_STYLES}</style>${withButtons}`;
  });
}

Reveal.initialize({
  navigationMode: 'linear',
  width: 1400,
  height: 800,
  hash: true,
  slideNumber: true,
  plugins: [RevealMarkdown, RevealHighlight, RevealNotes],
}).then(enhanceCommandNotes);
