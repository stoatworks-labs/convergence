// The page's half of the crosscheck: fetch the job and its frames from
// crosscheck.py's server, render each run through window.convergenceDemo.renderRun
// (a fresh instance, the harness's clock, the same cues), work out the port's
// CPU numbers through window.convergenceDemo.cpu, erf and gauss, and post it
// all back, with the page's table of controls and any uniform the page set
// that a program lacks or a program has that nothing set. crosscheck.py
// compares; nothing is judged here.
const post = (path, body) => fetch(path, { method: 'POST', body });

async function main() {
  for (let i = 0; i < 400 && !window.convergenceDemo; i += 1) await new Promise((r) => setTimeout(r, 50));
  if (!window.convergenceDemo) throw new Error('window.convergenceDemo never appeared: the page did not start');
  const demo = window.convergenceDemo;
  const job = await (await fetch('/xc/job.json')).json();
  const frames = [];
  for (let f = 0; f < job.frames; f += 1) {
    frames.push(new Uint8Array(await (await fetch(`/xc/frame/${f}`)).arrayBuffer()));
  }
  await post('/xc/table', JSON.stringify(demo.table));
  const missing = new Set();
  const unset = new Map();
  const errors = [];
  const refused = [];
  for (const run of job.runs) {
    const out = demo.renderRun({
      settings: run.settings,
      changes: run.changes,
      width: job.width,
      height: job.height,
      fps: run.fps,
      frames: frames.slice(0, run.frames),
      negative: run.negative ?? null,
    });
    out.missing.forEach((name) => missing.add(name));
    out.unset.forEach((name) => unset.set(name, (unset.get(name) ?? 0) + 1));
    if (out.error) errors.push(`${run.name}: GL error ${out.error}`);
    if (!out.ok) refused.push(run.name);
    const all = new Uint8Array(out.pictures.reduce((n, p) => n + p.length, 0));
    let at = 0;
    for (const p of out.pictures) { all.set(p, at); at += p.length; }
    await post(`/xc/out/${run.name}`, all);
    await post(`/xc/stats/${run.name}`, JSON.stringify(out.stats));
  }
  await post('/xc/cpu', JSON.stringify(job.cpu.map((c) => demo.cpu(c))));
  await post('/xc/erf', JSON.stringify(demo.erf(job.erf)));
  await post('/xc/gauss', JSON.stringify(demo.gauss(job.sigmas)));
  const never = [...unset].filter(([, n]) => n === job.runs.length).map(([name]) => name);
  await post('/xc/missing', JSON.stringify({ missing: [...missing], unset: never, errors, refused }));
  await post('/xc/done', 'ok');
}

main().catch((error) => post('/xc/error', String(error?.stack ?? error)));
