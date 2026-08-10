import { Vulpin } from './vulpin.js'

export const SAMPLES = [
  {
    name: 'hello world',
    src: `G "Hello World!"`,
  },
  {
    name: 'math',
    src: `G 5 + 3
G (2 + 3) * 4
G -3 + 10`,
  },
  {
    name: 'variables',
    src: `name = "Vul"
age = 21
G "Hi, " + name + ", age " + age`,
  },
  {
    name: 'add & replace',
    src: `E x = 3
A "x" + 2
G x
S "name" "Vul" "VUL"
G name`,
  },
  {
    name: 'string helpers',
    src: `E s = "hi there"
G s.U
G s.T
G s.C
G len(s)`,
  },
  {
    name: 'if / else',
    src: `E age = 17
? age >= 18
  G "you can vote"
:
  G "too young"
;`,
  },
  {
    name: 'while loop',
    src: `E x = 0
@ x < 4
  G x
  E x = x + 1
&`,
  },
  {
    name: 'for-range',
    src: `O i 1 5
  G i
&`,
  },
  {
    name: 'countdown',
    src: `O x 10 0 -2
  G x
&`,
  },
  {
    name: 'switch / case',
    src: `fruit = "apple"
W fruit
V "banana"
  G "yellow"
V "apple"
  G "red or green"
N
  G "unknown"
Z`,
  },
  {
    name: 'functions',
    src: `F add(a, b)
  R a + b
~
G add(3, 4)

F greet(name)
  G "Hello " + name
~
greet("World")`,
  },
  {
    name: 'recursion',
    src: `F fact(n)
  ? n <= 1
    R 1
  ;
  R n * fact(n - 1)
~
G fact(6)`,
  },
  {
    name: 'jumps',
    src: `J end
G "Skipped"
L end
G "Done"`,
  },
  {
    name: 'error handling',
    src: `T
  x = 10
  y = 0
  G x / y
C "err"
  G "Error: " + err
Y
G "Continues..."`,
  },
  {
    name: 'fizzbuzz',
    src: `O i 1 15
  ? i % 15 == 0
    G "fizzbuzz"
  :
  ? i % 3 == 0
    G "fizz"
  :
  ? i % 5 == 0
    G "buzz"
  :
    G i
  ;
  ;
  ;
&`,
  },
  {
    name: 'builtins',
    src: `G abs(-7)
G pow(2, 10)
G sqrt(81)
G len("vulpin")
G rand(100)`,
  },
]

export const state = {
  src: SAMPLES[0].src,
  logs: [],
}

const srcEl = document.getElementById('demoSrc')
const outEl = document.getElementById('demoOut')
const runBtn = document.getElementById('demoRun')
const statusEl = document.getElementById('demoStatus')
const samplesEl = document.getElementById('demoSamples')

function esc(s) {
  return s.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;')
}

function renderLines(lines) {
  return lines.map((l) => `<span class="ln">${l}</span>`).join('')
}

export function setSource(src) {
  state.src = src
  if (srcEl) srcEl.value = src
}

export function runSource(src, opts = {}) {
  const v = new Vulpin()
  const res = v.run(src, 40000)
  const lines = []
  if (res.ok && res.quit) {
    // output already collected; QUIT just stops cleanly
  }
  if (!res.ok) {
    lines.push(`<span class="tc-err">error: ${esc(res.error)}</span>`)
  }
  v.output.forEach((o) => lines.push(`<span class="tc-out">${esc(o)}</span>`))
  if (res.ms !== undefined && !opts.silent) {
    lines.push(`<span class="tc-mut mut">— done in ${res.ms} ms —</span>`)
  }
  return { lines, res }
}

export function run(srcElVal) {
  const src = srcElVal ?? (srcEl ? srcEl.value : state.src)
  if (srcEl && !srcElVal) srcEl.value = src
  const { lines } = runSource(src)
  if (outEl) outEl.innerHTML = `<pre>${renderLines(lines)}</pre>`
  if (statusEl) statusEl.textContent = 'ran it'
}

function showError(e) {
  if (outEl) outEl.innerHTML = `<pre>${renderLines([`<span class="tc-err">ui error: ${esc(e.message)}</span>`])}</pre>`
}

function buildSamples() {
  if (!samplesEl) return
  SAMPLES.forEach((s, i) => {
    const b = document.createElement('button')
    b.className = 'chip'
    b.type = 'button'
    b.textContent = `${i + 1}. ${s.name}`
    b.title = s.src
    b.addEventListener('click', () => {
      setSource(s.src)
      run(s.src)
      if (statusEl) statusEl.textContent = `sample ${i + 1}: ${s.name}`
    })
    samplesEl.appendChild(b)
  })
}

function init() {
  buildSamples()
  if (srcEl) {
    srcEl.value = SAMPLES[0].src
    srcEl.addEventListener('keydown', (e) => {
      if ((e.ctrlKey || e.metaKey) && e.key === 'Enter') { e.preventDefault(); run() }
    })
    srcEl.addEventListener('input', () => { if (statusEl) statusEl.textContent = 'unsaved edits' })
  }
  if (runBtn) runBtn.addEventListener('click', () => run())

  // a typed-input sample needs browser prompt; show it last
  const kIdx = SAMPLES.length
  SAMPLES.push({
    name: 'typed input (K)',
    src: `K age "how old are you? " I
G "cool, " + age`,
  })
  const kb = document.createElement('button')
  kb.className = 'chip'
  kb.type = 'button'
  kb.textContent = `${kIdx + 1}. typed input (K)`
  kb.addEventListener('click', () => {
    setSource(SAMPLES[kIdx].src)
    run(SAMPLES[kIdx].src)
    if (statusEl) statusEl.textContent = 'sample: typed input (K)'
  })
  if (samplesEl) samplesEl.appendChild(kb)

  run(SAMPLES[0].src)
}

if (document.readyState === 'loading') {
  document.addEventListener('DOMContentLoaded', init)
} else {
  init()
}

window.__vulpinDemo = { run, setSource, runSource, SAMPLES }