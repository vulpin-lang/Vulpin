// Tiny in-browser Vulpin interpreter powering the site playground.
// It mirrors the real C VM's command set so every documented command
// runs here: G/g/P output, K typed input, E/- bare assignment,
// A add-assign, S string replace, ?/:/; if-else, @/& while, O/& for-range,
// W/V/N/Z switch, J/L jumps, F/R/~ functions, T/C/Y try-catch, U imports,
// D delay, Q quit, string shortcuts .U .L .S .T .C, bare expressions.

export class Vulpin {
  constructor() {
    this.reset()
  }

  reset() {
    this.env = Object.create(null)
    this.output = []
    this.builtins = {
      abs: Math.abs,
      floor: Math.floor,
      ceil: Math.ceil,
      round: Math.round,
      sqrt: Math.sqrt,
      pow: Math.pow,
      len: (s) => String(s).length,
      upper: (s) => String(s).toUpperCase(),
      lower: (s) => String(s).toLowerCase(),
      title: (s) => String(s).replace(/\w\S*/g, (t) => t[0].toUpperCase() + t.slice(1).toLowerCase()),
      char: (n) => String.fromCharCode(n),
      ord: (c) => (typeof c === 'string' ? c.charCodeAt(0) : c),
      rand: (n) => Math.floor(Math.random() * (n || 100)),
      min: Math.min,
      max: Math.max,
    }
  }

  run(source, maxSteps = 40000, freshEnv = true) {
    if (freshEnv) this.reset()
    const lines = source.replace(/\r\n/g, '\n').split('\n')
    const src = lines.map((l) => l.replace(/\s+$/, ''))
    let pc = 0
    let steps = 0
    const started = Date.now()
    try {
      while (pc < src.length) {
        if (++steps > maxSteps) throw new Error('sandbox: loop bound hit')
        pc = this.step(src, pc)
      }
      return { ok: true, ms: Date.now() - started }
    } catch (e) {
      if (e.message === 'QUIT::') return { ok: true, quit: true, ms: Date.now() - started }
      return { ok: false, ms: Date.now() - started, error: e.message }
    }
  }

  step(src, pc) {
    const raw = src[pc]
    const line = raw.replace(/^\s+/, '')
    const cmd = line[0]
    const arg = line.slice(1).trim()

    switch (cmd) {
      case '':
      case '#':
        return pc + 1

      case 'G':
        this.println(this.evalExpr(arg))
        return pc + 1

      case 'g':
      case 'P':
        this.print(this.evalExpr(arg))
        return pc + 1

      case 'E': {
        const m = arg.match(/^([A-Za-z_]\w*)\s*(?:=\s*)?([\s\S]*)$/)
        if (!m || !m[1]) throw new Error('E needs a variable name')
        this.env[m[1]] = this.evalExpr(m[2])
        return pc + 1
      }

      case 'A': {
        const m = arg.match(/^(?:([A-Za-z_]\w*)|"([^"]*)")\s*([\s\S]*)$/)
        if (!m || (!m[1] && !m[2])) throw new Error('A needs a variable name')
        const name = m[1] || m[2]
        const cur = this.env[name] === undefined ? 0 : this.env[name]
        this.env[name] = cur + this.evalExpr(m[3])
        return pc + 1
      }

      case 'S': {
        const m = arg.match(/^"([^"]*)"\s*"([^"]*)"\s*"([^"]*)"$/)
        if (!m) throw new Error('S format: S"var""from""to"')
        const name = m[1]
        const cur = this.env[name] === undefined ? '' : this.env[name]
        this.env[name] = String(cur).split(m[2]).join(m[3])
        return pc + 1
      }

      case 'D': {
        const del = arg.match(/^"([^"]*)"$/)
        if (del) { delete this.env[del[1]]; return pc + 1 }
        return pc + 1 // delay waits in the C runtime; skipped here
      }

      case 'K': {
        const m = arg.match(/^([A-Za-z_]\w*)\s*(?:"([^"]*)")?\s*([IFNLWEUAP]?)$/)
        if (!m || !m[1]) throw new Error('K needs a variable name')
        let raw = ''
        if (typeof prompt === 'function') {
          const ans = prompt(m[2] === undefined || m[2] === '' ? 'input: ' : m[2])
          if (ans !== null) raw = ans
        }
        this.env[m[1]] = this.coerceInput(raw, m[3])
        return pc + 1
      }

      case 'Q':
        throw new Error('QUIT::')

      case '?': {
        // conditional jump: ? cond J label
        const jm = arg.match(/^(.*)\s+J\s+([A-Za-z_]\w*)$/)
        if (jm) {
          if (this.truthy(this.evalExpr(jm[1]))) return this.findLabel(src, jm[2])
          return pc + 1
        }
        const cond = this.truthy(this.evalExpr(arg))
        if (cond) return pc + 1
        let depth = 1
        let i = pc + 1
        for (; i < src.length; i++) {
          const c = src[i].trim()[0]
          if (c === '?') depth++
          else if (c === ':') { depth--; if (depth === 0) return i + 1 }
          else if (c === ';') { depth--; if (depth === 0) return i + 1 }
        }
        return i + 1
      }

      case ':': // hit while executing then-branch: skip to ';'
        return this.skipTo(src, pc, ';')

      case ';':
        return pc + 1

      case '@': {
        const cond = this.truthy(this.evalExpr(arg))
        if (cond) return pc + 1
        return this.findLoopEnd(src, pc) + 1 // skip past the closing &
      }

      case '&':
        return this.findLoopStart(src, pc)

      case 'O': { // O var start end [step] ... &
        const parts = arg.split(/\s+/).filter(Boolean)
        const [vname, startT, endT] = parts
        if (!/^[A-Za-z_]\w*$/.test(vname || '')) throw new Error('O needs a variable')
        const start = this.num(this.evalExpr(startT))
        const end = this.num(this.evalExpr(endT))
        const step = parts[3] ? Math.abs(this.num(this.evalExpr(parts[3]))) || 1 : 1
        let endIdx = this.findLoopEnd(src, pc)
        const body = src.slice(pc + 1, endIdx).join('\n')
        const dir = start <= end ? 1 : -1
        for (let v = start; dir === 1 ? v <= end : v >= end; v += dir * step) {
          this.env[vname] = v
          const sub = new Vulpin()
          sub.env = this.env
          sub.builtins = this.builtins
          const out = sub.run(body, 2000, false)
          if (!out.ok) throw new Error('in loop: ' + out.error)
          this.output.push(...sub.output)
          sub.output = []
        }
        return endIdx + 1
      }

      case 'W': {
        const target = this.evalExpr(arg)
        const endIdx = this.findSwitchEnd(src, pc)
        for (let i = pc + 1; i < endIdx; i++) {
          const c = src[i].trim()
          const ch = c[0]
          if (ch === 'V') {
            const cv = this.evalExpr(c.slice(1).trim())
            if (this.eq(cv, target)) {
              let j = i + 1
              for (; j < endIdx; j++) {
                const cc = src[j].trim()[0]
                if (cc === 'V' || cc === 'N' || cc === 'Z') break
              }
              return this.runBlock(src, i + 1, j, endIdx + 1, 'switch case')
            }
            i++
            continue
          }
          if (ch === 'N') {
            return this.runBlock(src, i + 1, endIdx, endIdx + 1, 'switch default')
          }
        }
        return endIdx + 1
      }

      case 'V':
      case 'N':
      case 'Z':
        return pc + 1 // only meaningful inside W

      case 'F': {
        const m = arg.match(/^([A-Za-z_]\w*)\s*(?:\(\s*((?:[A-Za-z_]\w*\s*,?\s*)*)\s*\))?\s*$/)
        if (!m || !m[1]) throw new Error('F needs a name (F name(a, b))')
        const name = m[1]
        const params = (m[2] || '').split(',').map((s) => s.trim()).filter(Boolean)
        let i = pc + 1
        for (; i < src.length; i++) if (src[i].trim()[0] === '~') break
        this.env['fn:' + name] = { params, body: src.slice(pc + 1, i) }
        return i + 1
      }

      case 'R':
        this.retval = this.evalExpr(arg)
        throw new Error('::return::')

      case 'Y':
        return pc + 1 // only meaningful inside T

      case 'T': {
        const endIdx = this.findTryEnd(src, pc)
        let ci = -1, ename = 'err'
        for (let j = pc + 1; j < endIdx; j++) {
          const c = src[j].trim()
          if (c[0] === 'C') {
            const m = c.slice(1).trim().match(/^"([A-Za-z_]\w*)"$/)
            if (m) { ci = j; ename = m[1] }
            break
          }
        }
        const tryBody = src.slice(pc + 1, ci >= 0 ? ci : endIdx)
        const catchBody = ci >= 0 ? src.slice(ci + 1, endIdx) : []
        const sub = new Vulpin()
        sub.env = this.env
        sub.builtins = this.builtins
        const out = sub.run(tryBody.join('\n'), 4000, false)
        if (!out.ok) {
          sub.env[ename] = out.error
          const cs = new Vulpin()
          cs.env = sub.env
          cs.builtins = this.builtins
          const c2 = cs.run(catchBody.join('\n'), 4000, false)
          if (!c2.ok && c2.error !== 'QUIT::') throw new Error('in catch: ' + c2.error)
          this.output.push(...cs.output)
        } else {
          this.output.push(...sub.output)
        }
        return endIdx + 1
      }

      case 'C':
      case 'L':
      case '~':
        return pc + 1

      case 'J': {
        let i
        for (i = 0; i < src.length; i++) {
          const m = src[i].trim().match(/^L\s+([A-Za-z_]\w*)\s*$/)
          if (m && m[1] === arg) break
        }
        return i
      }

      case 'U':
        return pc + 1 // imports are a no-op in the browser sandbox

      default:
        // bare assignment: name = expr
        const am = line.match(/^([A-Za-z_]\w*)\s*=\s*([\s\S]*)$/)
        if (am) { this.env[am[1]] = this.evalExpr(am[2]); return pc + 1 }
        // bare expression / function call
        const v = this.evalExpr(line)
        if (v !== undefined && v !== '') this.println(v)
        return pc + 1
    }
  }

  /* ---------- block scans ---------- */

  skipTo(src, pc, target) {
    let depth = 0
    let i = pc + 1
    for (; i < src.length; i++) {
      const c = src[i].trim()[0]
      if (c === '?') depth++
      else if (c === target) {
        if (depth === 0) return i + 1
        depth--
      }
    }
    return i
  }

  findLoopEnd(src, pc) {
    let depth = 1
    let i = pc + 1
    for (; i < src.length; i++) {
      const c = src[i].trim()[0]
      if (c === '@' || c === 'O') depth++
      else if (c === '&' && --depth === 0) break
    }
    return i
  }

  findLabel(src, label) {
    for (let i = 0; i < src.length; i++) {
      const m = src[i].trim().match(/^L\s+([A-Za-z_]\w*)\s*$/)
      if (m && m[1] === label) return i
    }
    return src.length
  }

  findLoopStart(src, pc) {
    let depth = 1
    let i = pc - 1
    for (; i >= 0; i--) {
      const c = src[i].trim()[0]
      if (c === '&') depth++
      else if ((c === '@' || c === 'O') && --depth === 0) break
    }
    return i
  }

  findSwitchEnd(src, pc) {
    let depth = 1
    let i = pc + 1
    for (; i < src.length; i++) {
      const c = src[i].trim()[0]
      if (c === 'W') depth++
      else if (c === 'Z' && --depth === 0) break
    }
    return i
  }

  findTryEnd(src, pc) {
    let depth = 1
    let i = pc + 1
    for (; i < src.length; i++) {
      const c = src[i].trim()[0]
      if (c === 'T') depth++
      else if (c === 'Y' && --depth === 0) break
    }
    return i
  }

  runBlock(src, from, to, retPc, label) {
    const sub = new Vulpin()
    sub.env = this.env
    sub.builtins = this.builtins
    const out = sub.run(src.slice(from, to).join('\n'), 4000, false)
    if (!out.ok && out.error !== '::return::') throw new Error('in ' + label + ': ' + out.error)
    if (sub.retval !== undefined) return retPc // propagate
    this.output.push(...sub.output)
    return retPc
  }

  /* ---------- expressions ---------- */

  evalExpr(s) {
    if (!s || !s.trim()) return ''
    this.toks = s.match(
      /"[^"\\]*(?:\\.[^"\\]*)*"|'[^'\\]*(?:\\.[^'\\]*)*'|\d+\.\d+|\d+|<=|>=|==|!=|\.|\+\+|--|[+\-*/%()<>,]|[A-Za-z_]\w*/g
    ) || []
    this.pos = 0
    const v = this.sum()
    if (this.pos < this.toks.length) throw new Error('unexpected ' + JSON.stringify(this.toks[this.pos]))
    return v
  }

  peek() { return this.toks[this.pos] }
  take() { return this.toks[this.pos++] }

  sum() {
    let a = this.term()
    for (;;) {
      const t = this.peek()
      if (t === '+') { this.take(); a = this.arith(a, this.term(), '+') }
      else if (t === '-') { this.take(); a = this.arith(a, this.term(), '-') }
      else if (t === '==') { this.take(); a = this.eq(a, this.term()) ? 1 : 0 }
      else if (t === '!=') { this.take(); a = this.eq(a, this.term()) ? 0 : 1 }
      else if (t === '>=') { this.take(); a = this.num(a) >= this.num(this.term()) ? 1 : 0 }
      else if (t === '<=') { this.take(); a = this.num(a) <= this.num(this.term()) ? 1 : 0 }
      else if (t === '>') { this.take(); a = this.num(a) > this.num(this.term()) ? 1 : 0 }
      else if (t === '<') { this.take(); a = this.num(a) < this.num(this.term()) ? 1 : 0 }
      else break
    }
    return a
  }

  term() {
    let left = this.factor()
    for (;;) {
      const t = this.peek()
      if (t === '*') { this.take(); left = this.arith(left, this.factor(), '*') }
      else if (t === '/') { this.take(); left = this.arith(left, this.factor(), '/') }
      else if (t === '%') { this.take(); left = this.num(left) % this.num(this.factor()) }
      else break
    }
    return left
  }

  factor() {
    const t = this.take()
    if (t === undefined) return ''
    if (t === '(') {
      const v = this.sum()
      if (this.peek() === ')') this.take()
      return v
    }
    if (t === '-' || t === '+') {
      const v = this.num(this.factor())
      return t === '-' ? -v : v
    }
    if (t[0] === '"' || t[0] === "'") {
      let s = this.unquote(t)
      if (this.peek() === '.') {
        this.take()
        const m = this.take()
        if (m === 'U') s = String(s).toUpperCase()
        else if (m === 'L') s = String(s).toLowerCase()
        else if (m === 'S') s = String(s).trim()
        else if (m === 'T') s = String(s).replace(/\w\S*/g, (w) => w[0].toUpperCase() + w.slice(1).toLowerCase())
        else if (m === 'C') s = String(s).charAt(0).toUpperCase() + String(s).slice(1).toLowerCase()
      }
      return s
    }
    if (this.isNum(t)) return parseFloat(t)
    if (t === 'true') return true
    if (t === 'false') return false
    if (this.peek() === '(') {
      this.take()
      const args = []
      if (this.peek() !== ')') {
        args.push(this.sum())
        while (this.peek() === ',') { this.take(); args.push(this.sum()) }
      }
      if (this.peek() === ')') this.take()
      return this.call(t, args)
    }
    if (this.peek() === '.') {
      // variable string shortcut: s.U
      this.take()
      const m = this.take()
      if (this.env[t] !== undefined) {
        let v = this.env[t]
        const s = String(v)
        if (m === 'U') v = s.toUpperCase()
        else if (m === 'L') v = s.toLowerCase()
        else if (m === 'S') v = s.trim()
        else if (m === 'T') v = s.replace(/\w\S*/g, (w) => w[0].toUpperCase() + w.slice(1).toLowerCase())
        else if (m === 'C') v = s.charAt(0).toUpperCase() + s.slice(1).toLowerCase()
        return v
      }
      return 0
    }
    if (t in this.env) return this.env[t]
    if (this.builtins[t]) return this.builtins[t]
    return 0
  }

  call(name, args) {
    const fn = this.env['fn:' + name]
    if (fn) {
      const sub = new Vulpin()
      sub.env = Object.assign(Object.create(null), this.env)
      sub.builtins = this.builtins
      fn.params.forEach((p, j) => { sub.env[p] = args[j] })
      const out = sub.run(fn.body.join('\n'), 2000, false)
      if (!out.ok && out.error !== '::return::') throw new Error('inside ' + name + ': ' + out.error)
      if (sub.retval !== undefined) return sub.retval
      if (!out.ok && out.error !== '::return::') throw new Error('inside ' + name + ': ' + out.error)
      return sub.output.join('')
    }
    const b = this.builtins[name]
    if (b) return b(...args.map((x) => (typeof x === 'number' ? x : x)))
    return args[0] ?? 0
  }

  isNum(t) { return /^-?\d/.test(t) }
  coerceInput(raw, type) {
    const s = String(raw).trim()
    switch (type) {
      case 'I': { const n = parseInt(s, 10); return Number.isNaN(n) ? 0 : n }
      case 'F': { const n = parseFloat(s); return Number.isNaN(n) ? 0.0 : n }
      case 'N': { const n = parseFloat(s); return Number.isNaN(n) ? 0 : n }
      case 'L': return /^[a-zA-Z]$/.test(s) ? s : ''
      case 'W': return /^[a-zA-Z]+$/.test(s) ? s : ''
      case 'E': return /^[a-z]+$/.test(s) ? s : ''
      case 'U': return /^[A-Z]+$/.test(s) ? s : ''
      case 'A': return /^[a-zA-Z ]+$/.test(s) ? s : ''
      case 'P': return /^[a-zA-Z0-9 ]+$/.test(s) ? s : ''
      default: return s
    }
  }
  num(v) {
    if (typeof v === 'string') {
      if (v === 'true') return 1
      if (v === 'false') return 0
      const n = parseFloat(v)
      return Number.isNaN(n) ? 0 : n
    }
    return v
  }
  arith(a, b, op) {
    if (typeof a === 'string' || typeof b === 'string') {
      if (op === '+') return String(a) + String(b)
      return a
    }
    if ((op === '/' || op === '%') && b === 0) throw new Error('division by zero')
    switch (op) {
      case '+': return a + b
      case '-': return a - b
      case '*': return a * b
      case '/': return Math.floor(a / b)
    }
  }
  eq(a, b) { return String(a) === String(b) }
  unquote(t) {
    return t.slice(1, -1).replace(/\\"/g, '"').replace(/\\'/g, "'").replace(/\\n/g, '\n')
  }
  truthy(v) { return !(v === 0 || v === false || v === '' || v === undefined || v === null) }
  println(v) { this.output.push(this.fmt(v)) }
  print(v) {
    const s = this.fmt(v)
    if (this.output.length) this.output[this.output.length - 1] += s
    else this.output.push(s)
  }
  fmt(v) {
    if (typeof v === 'boolean') return v ? '1' : '0'
    if (typeof v === 'string') return v
    return String(v)
  }
}