// The page's half of the flash measurement: fetch the cases from flash.py's
// server, run each through window.convergenceDemo.flashRun (the page's own
// clips, a fresh instance, the page's clock arithmetic, the visitor's moves
// as cues) and post the per-frame levels back. flash.py counts the flashes.
const post = (path, body) => fetch(path, { method: 'POST', body });

async function main() {
  for (let i = 0; i < 400 && !window.convergenceDemo; i += 1) await new Promise((r) => setTimeout(r, 50));
  if (!window.convergenceDemo) throw new Error('window.convergenceDemo never appeared: the page did not start');
  const job = await (await fetch('/fc/job.json')).json();
  for (const c of job.cases) {
    const out = window.convergenceDemo.flashRun({ ...c, width: job.width, height: job.height, fps: job.fps, seconds: job.seconds });
    if (out.error) throw new Error(`${c.name}: GL error ${out.error}`);
    await post(`/fc/levels/${encodeURIComponent(c.name)}`, JSON.stringify(out.levels));
  }
  await post('/fc/done', 'ok');
}

main().catch((error) => post('/fc/error', String(error?.stack ?? error)));
