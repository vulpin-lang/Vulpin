use std::collections::HashMap;
use std::io::{self, Write};
use std::sync::atomic::{AtomicU64, Ordering};

static RANDOM_SEED: AtomicU64 = AtomicU64::new(0);

fn next_random() -> u64 {
    let mut seed = RANDOM_SEED.load(Ordering::Relaxed);
    if seed == 0 {
        seed = std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .unwrap()
            .as_nanos() as u64;
    }
    seed = seed
        .wrapping_mul(6364136223846793005)
        .wrapping_add(1442695040888963407);
    RANDOM_SEED.store(seed, Ordering::Relaxed);
    seed
}

#[derive(Debug, Clone, PartialEq)]
enum Value {
    Int(i64),
    Float(f64),
    Str(String),
    Bool(bool),
    Module(String),
    None,
}

impl Value {
    fn as_bool(&self) -> bool {
        match self {
            Value::Bool(b) => *b,
            Value::Int(i) => *i != 0,
            Value::Float(f) => *f != 0.0,
            Value::Str(s) => !s.is_empty(),
            Value::Module(_) => true,
            Value::None => false,
        }
    }
}

fn value_to_string(v: &Value) -> String {
    match v {
        Value::Int(i) => i.to_string(),
        Value::Float(f) => {
            if f.fract() == 0.0 {
                format!("{:.1}", f)
            } else {
                f.to_string()
            }
        }
        Value::Str(s) => s.clone(),
        Value::Bool(b) => b.to_string(),
        Value::Module(m) => format!("<module '{}'>", m),
        Value::None => "None".to_string(),
    }
}

#[derive(Debug, Clone, PartialEq)]
enum Token {
    Num(f64),
    Str(String),
    Var(String),
    Ident(String),
    Op(String),
    LParen,
    RParen,
    Comma,
    Dot,
    Eof,
}

fn tokenize_expr(s: &str) -> Result<Vec<Token>, String> {
    let mut tokens = Vec::new();
    let mut chars = s.chars().peekable();
    while let Some(&c) = chars.peek() {
        if c.is_whitespace() {
            chars.next();
        } else if c.is_ascii_digit()
            || (c == '.'
            && chars
            .clone()
            .nth(1)
            .map_or(false, |d| d.is_ascii_digit()))
        {
            let mut num_s = String::new();
            while let Some(&d) = chars.peek() {
                if d.is_ascii_digit() || d == '.' {
                    num_s.push(d);
                    chars.next();
                } else {
                    break;
                }
            }
            tokens.push(Token::Num(num_s.parse().unwrap()));
        } else if c == '"' {
            chars.next();
            let mut str_s = String::new();
            while let Some(&d) = chars.peek() {
                if d == '\\' {
                    chars.next();
                    if let Some(&esc) = chars.peek() {
                        match esc {
                            'n' => str_s.push('\n'),
                            't' => str_s.push('\t'),
                            '"' => str_s.push('"'),
                            '\\' => str_s.push('\\'),
                            _ => {
                                str_s.push('\\');
                                str_s.push(esc);
                            }
                        }
                        chars.next();
                    }
                } else if d == '"' {
                    chars.next();
                    break;
                } else {
                    str_s.push(d);
                    chars.next();
                }
            }
            tokens.push(Token::Str(str_s));
        } else if c == '$' {
            chars.next();
            let mut var_s = String::new();
            while let Some(&d) = chars.peek() {
                if d.is_alphanumeric() || d == '_' {
                    var_s.push(d);
                    chars.next();
                } else {
                    break;
                }
            }
            tokens.push(Token::Var(var_s));
        } else if c.is_alphabetic() || c == '_' {
            let mut id_s = String::new();
            while let Some(&d) = chars.peek() {
                if d.is_alphanumeric() || d == '_' {
                    id_s.push(d);
                    chars.next();
                } else {
                    break;
                }
            }
            tokens.push(Token::Ident(id_s));
        } else if "+-*/=<>!".contains(c) {
            let mut op_s = String::new();
            op_s.push(c);
            chars.next();
            if let Some(&next_c) = chars.peek() {
                if (c == '<' || c == '>' || c == '!' || c == '=') && next_c == '=' {
                    op_s.push(next_c);
                    chars.next();
                }
            }
            tokens.push(Token::Op(op_s));
        } else if c == '(' {
            tokens.push(Token::LParen);
            chars.next();
        } else if c == ')' {
            tokens.push(Token::RParen);
            chars.next();
        } else if c == ',' {
            tokens.push(Token::Comma);
            chars.next();
        } else if c == '.' {
            tokens.push(Token::Dot);
            chars.next();
        } else {
            return Err(format!("Unexpected char: {}", c));
        }
    }
    tokens.push(Token::Eof);
    Ok(tokens)
}

#[derive(Debug, Clone)]
enum Expr {
    Num(f64),
    Str(String),
    Var(String),
    BinOp(Box<Expr>, String, Box<Expr>),
    UnaryOp(String, Box<Expr>),
    FuncCall(String, Vec<Expr>),
    MethodCall(Box<Expr>, String),
    MethodCallWithArgs(Box<Expr>, String, Vec<Expr>),
}

struct ExprParser {
    tokens: Vec<Token>,
    pos: usize,
}

impl ExprParser {
    fn new(tokens: Vec<Token>) -> Self {
        Self { tokens, pos: 0 }
    }
    fn peek(&self) -> &Token {
        &self.tokens[self.pos]
    }
    fn next(&mut self) -> Token {
        let t = self.tokens[self.pos].clone();
        self.pos += 1;
        t
    }
    fn expect(&mut self, t: Token) -> Result<(), String> {
        if self.next() == t {
            Ok(())
        } else {
            Err("Unexpected token".into())
        }
    }

    fn parse(&mut self) -> Result<Expr, String> {
        self.parse_equality()
    }

    fn parse_equality(&mut self) -> Result<Expr, String> {
        let mut left = self.parse_comparison()?;
        while let Token::Op(op) = self.peek() {
            if op == "=" || op == "!=" {
                let op_str = op.clone();
                self.next();
                let right = self.parse_comparison()?;
                left = Expr::BinOp(Box::new(left), op_str, Box::new(right));
            } else {
                break;
            }
        }
        Ok(left)
    }

    fn parse_comparison(&mut self) -> Result<Expr, String> {
        let mut left = self.parse_term()?;
        while let Token::Op(op) = self.peek() {
            if op == "<" || op == ">" || op == "<=" || op == ">=" {
                let op_str = op.clone();
                self.next();
                let right = self.parse_term()?;
                left = Expr::BinOp(Box::new(left), op_str, Box::new(right));
            } else {
                break;
            }
        }
        Ok(left)
    }

    fn parse_term(&mut self) -> Result<Expr, String> {
        let mut left = self.parse_factor()?;
        while let Token::Op(op) = self.peek() {
            if op == "+" || op == "-" {
                let op_str = op.clone();
                self.next();
                let right = self.parse_factor()?;
                left = Expr::BinOp(Box::new(left), op_str, Box::new(right));
            } else {
                break;
            }
        }
        Ok(left)
    }

    fn parse_factor(&mut self) -> Result<Expr, String> {
        let mut left = self.parse_unary()?;
        while let Token::Op(op) = self.peek() {
            if op == "*" || op == "/" {
                let op_str = op.clone();
                self.next();
                let right = self.parse_unary()?;
                left = Expr::BinOp(Box::new(left), op_str, Box::new(right));
            } else {
                break;
            }
        }
        Ok(left)
    }

    fn parse_unary(&mut self) -> Result<Expr, String> {
        if let Token::Op(op) = self.peek() {
            if op == "-" || op == "!" {
                let op_str = op.clone();
                self.next();
                let right = self.parse_unary()?;
                return Ok(Expr::UnaryOp(op_str, Box::new(right)));
            }
        }
        self.parse_primary()
    }

    fn parse_arg_list(&mut self) -> Result<Vec<Expr>, String> {
        let mut args = Vec::new();
        if let Token::RParen = self.peek() {
            self.next();
        } else {
            args.push(self.parse()?);
            while let Token::Comma = self.peek() {
                self.next();
                args.push(self.parse()?);
            }
            self.expect(Token::RParen)?;
        }
        Ok(args)
    }

    fn parse_primary(&mut self) -> Result<Expr, String> {
        let mut expr = match self.next() {
            Token::Num(n) => Expr::Num(n),
            Token::Str(s) => Expr::Str(s),
            Token::Var(v) => {
                if let Token::LParen = self.peek() {
                    self.next();
                    let args = self.parse_arg_list()?;
                    Expr::FuncCall(v, args)
                } else {
                    Expr::Var(v)
                }
            }
            Token::Ident(id) => {
                if let Token::LParen = self.peek() {
                    self.next();
                    let args = self.parse_arg_list()?;
                    Expr::FuncCall(id, args)
                } else {
                    Expr::Str(id)
                }
            }
            Token::LParen => {
                let expr = self.parse()?;
                self.expect(Token::RParen)?;
                expr
            }
            _ => return Err("Expected expression".into()),
        };

        while let Token::Dot = self.peek() {
            self.next();
            if let Token::Ident(method) = self.next() {
                if let Token::LParen = self.peek() {
                    self.next();
                    let args = self.parse_arg_list()?;
                    expr = Expr::MethodCallWithArgs(Box::new(expr), method, args);
                } else {
                    expr = Expr::MethodCall(Box::new(expr), method);
                }
            } else {
                return Err("Expected method name".into());
            }
        }
        Ok(expr)
    }
}

fn parse_expr_str(s: &str) -> Result<Expr, String> {
    let tokens = tokenize_expr(s)?;
    let mut parser = ExprParser::new(tokens);
    parser.parse()
}

struct ArgParser {
    tokens: Vec<Token>,
    pos: usize,
}

impl ArgParser {
    fn new(s: &str) -> Result<Self, String> {
        Ok(Self {
            tokens: tokenize_expr(s)?,
            pos: 0,
        })
    }
    fn next(&mut self) -> Token {
        let t = self.tokens[self.pos].clone();
        self.pos += 1;
        t
    }
    fn parse_string(&mut self) -> Result<String, String> {
        if let Token::Str(s) = self.next() {
            Ok(s)
        } else {
            Err("Expected string".into())
        }
    }
    fn parse_word(&mut self) -> Result<String, String> {
        match self.next() {
            Token::Ident(s) => Ok(s),
            Token::Op(s) => Ok(s),
            Token::Str(s) => Ok(s),
            _ => Err("Expected word".into()),
        }
    }
    #[allow(dead_code)]
    fn parse_expr(&mut self) -> Result<Expr, String> {
        let mut parser = ExprParser {
            tokens: self.tokens.clone(),
            pos: self.pos,
        };
        let expr = parser.parse()?;
        self.pos = parser.pos;
        Ok(expr)
    }
}

#[allow(dead_code)]
enum Statement {
    PrintNewline(Expr),
    Print(Expr),
    Assign(String, Expr),
    ArithAssign(String, String, Expr),
    StrReplace(String, Expr, Expr),
    Delay(Expr),
    Delete(String),
    Input(String, String, String),
    Quit,
    ErrorExit(Expr),
    Import(String),
    If(Expr),
    CondJump(Expr, String),
    Else,
    EndIf,
    While(Expr),
    EndLoop,
    ForLoop(String, Expr, Expr, Option<Expr>),
    Label(String),
    Jump(String),
    FunctionDef(String, Vec<String>),
    Return(Expr),
    EndFunc,
    Try,
    Catch(Option<String>),
    EndTry,
    Switch(Expr),
    Case(Expr),
    Default,
    EndSwitch,
    PythonExec(String),
    RustExec(String),
    ExprStmt(Expr),
    Comment,
}

fn strip_comment(line: &str) -> String {
    let mut in_string = false;
    let mut result = String::new();
    for c in line.chars() {
        if c == '"' {
            in_string = !in_string;
            result.push(c);
        } else if c == '#' && !in_string {
            break;
        } else {
            result.push(c);
        }
    }
    result
}

fn get_command_char(line: &str) -> char {
    let stripped = strip_comment(line);
    let trimmed = stripped.trim();
    if trimmed.is_empty() {
        return '#';
    }
    trimmed.chars().next().unwrap_or('#')
}

fn parse_statement(line: &str) -> Result<Statement, String> {
    let stripped = strip_comment(line);
    let line = stripped.trim();
    if line.is_empty() {
        return Ok(Statement::Comment);
    }

    if let Some(eq_pos) = line.find('=') {
        let before_eq = &line[..eq_pos];
        let after_eq = &line[eq_pos + 1..];
        if !before_eq.ends_with('<')
            && !before_eq.ends_with('>')
            && !before_eq.ends_with('!')
            && !before_eq.ends_with('=')
            && !after_eq.starts_with('=')
        {
            let lhs = before_eq.trim();
            if lhs.chars().all(|c| c.is_alphanumeric() || c == '_') && !lhs.is_empty() {
                if lhs.chars().next().unwrap().is_alphabetic()
                    || lhs.chars().next().unwrap() == '_'
                {
                    if lhs != "A" && lhs != "S" && lhs != "D" && lhs != "K" {
                        let expr_str = after_eq.trim();
                        let expr = parse_expr_str(expr_str)?;
                        return Ok(Statement::Assign(lhs.to_string(), expr));
                    }
                }
            }
        }
    }

    let mut chars = line.chars();
    let cmd = chars.next().unwrap();
    let rest = chars.as_str().trim_start();

    match cmd {
        'G' => Ok(Statement::PrintNewline(parse_expr_str(rest)?)),
        'P' => Ok(Statement::Print(parse_expr_str(rest)?)),
        'A' => {
            let mut p = ArgParser::new(rest)?;
            let var = p.parse_string()?;
            let op = p.parse_word()?;
            let expr = p.parse_expr()?;
            Ok(Statement::ArithAssign(var, op, expr))
        }
        'S' => {
            let mut p = ArgParser::new(rest)?;
            let var = p.parse_string()?;
            let old = p.parse_expr()?;
            let new = p.parse_expr()?;
            Ok(Statement::StrReplace(var, old, new))
        }
        'D' => {
            if rest.starts_with('"') {
                let mut p = ArgParser::new(rest)?;
                Ok(Statement::Delete(p.parse_string()?))
            } else {
                Ok(Statement::Delay(parse_expr_str(rest)?))
            }
        }
        'K' => {
            let mut p = ArgParser::new(rest)?;
            let var = p.parse_string()?;
            let prompt = p.parse_string()?;
            let type_char = p.parse_string().unwrap_or_else(|_| "W".to_string());
            Ok(Statement::Input(var, prompt, type_char))
        }
        'Q' => Ok(Statement::Quit),
        'E' => Ok(Statement::ErrorExit(parse_expr_str(rest)?)),
        'U' => {
            let mut p = ArgParser::new(rest)?;
            Ok(Statement::Import(p.parse_string()?))
        }
        '?' => {
            let mut j_pos = None;
            let mut in_str = false;
            let chars: Vec<char> = rest.chars().collect();
            for i in 0..chars.len() {
                if chars[i] == '"' {
                    in_str = !in_str;
                }
                if !in_str && chars[i] == 'J' {
                    let before_ok = i == 0 || chars[i - 1].is_whitespace();
                    let after_ok = i + 1 == chars.len() || chars[i + 1].is_whitespace();
                    if before_ok && after_ok {
                        j_pos = Some(i);
                    }
                }
            }
            if let Some(pos) = j_pos {
                let cond_str = &rest[..pos];
                let label = rest[pos + 1..].trim();
                Ok(Statement::CondJump(
                    parse_expr_str(cond_str)?,
                    label.to_string(),
                ))
            } else {
                Ok(Statement::If(parse_expr_str(rest)?))
            }
        }
        ':' => Ok(Statement::Else),
        ';' => Ok(Statement::EndIf),
        '@' => Ok(Statement::While(parse_expr_str(rest)?)),
        '&' => Ok(Statement::EndLoop),
        'O' => {
            let parts: Vec<&str> = rest.split_whitespace().collect();
            if parts.len() < 3 {
                return Err("ForLoop requires var, start, end".into());
            }
            let var = parts[0].to_string();
            let start = parse_expr_str(parts[1])?;
            let end = parse_expr_str(parts[2])?;
            let step = if parts.len() > 3 {
                Some(parse_expr_str(parts[3])?)
            } else {
                None
            };
            Ok(Statement::ForLoop(var, start, end, step))
        }
        'L' => {
            let mut p = ArgParser::new(rest)?;
            Ok(Statement::Label(p.parse_word()?))
        }
        'J' => {
            let mut p = ArgParser::new(rest)?;
            Ok(Statement::Jump(p.parse_word()?))
        }
        'F' => {
            let mut p = ArgParser::new(rest)?;
            let name = p.parse_word()?;
            let mut params = Vec::new();
            let rem = p.tokens[p.pos..]
                .iter()
                .map(|t| match t {
                    Token::Ident(s) => s.clone(),
                    Token::Var(s) => s.clone(),
                    Token::LParen => "(".to_string(),
                    Token::RParen => ")".to_string(),
                    Token::Comma => ",".to_string(),
                    _ => "".to_string(),
                })
                .collect::<Vec<_>>()
                .join("");

            if rem.starts_with('(') && rem.ends_with(')') {
                let inner = &rem[1..rem.len() - 1];
                for param in inner.split(',') {
                    let p_trim = param.trim();
                    if !p_trim.is_empty() {
                        params.push(p_trim.to_string());
                    }
                }
            }
            Ok(Statement::FunctionDef(name, params))
        }
        'R' => Ok(Statement::Return(parse_expr_str(rest)?)),
        '~' => Ok(Statement::EndFunc),
        'T' => Ok(Statement::Try),
        'C' => {
            if rest.is_empty() {
                Ok(Statement::Catch(None))
            } else {
                let mut p = ArgParser::new(rest)?;
                Ok(Statement::Catch(Some(p.parse_string()?)))
            }
        }
        'Y' => Ok(Statement::EndTry),
        'W' => Ok(Statement::Switch(parse_expr_str(rest)?)),
        'V' => Ok(Statement::Case(parse_expr_str(rest)?)),
        'N' => Ok(Statement::Default),
        'Z' => Ok(Statement::EndSwitch),
        '!' => {
            let py_code = stripped.trim_start().strip_prefix('!').unwrap_or("");
            Ok(Statement::PythonExec(py_code.to_string()))
        }
        '^' => {
            let rust_code = stripped.trim_start().strip_prefix('^').unwrap_or("");
            Ok(Statement::RustExec(rust_code.to_string()))
        }
        '#' => Ok(Statement::Comment),
        '$' => Ok(Statement::ExprStmt(parse_expr_str(line)?)),
        _ => {
            if let Ok(expr) = parse_expr_str(line) {
                Ok(Statement::ExprStmt(expr))
            } else {
                Err(format!("Unknown command: {}", cmd))
            }
        }
    }
}

#[derive(Clone)]
struct BlockInfo {
    matching_else: Option<usize>,
    matching_end: Option<usize>,
}

#[derive(Clone)]
enum LoopFrame {
    While(usize),
    For(usize, String, Value, Value),
}

enum VMError {
    Str(String),
    Return(Value),
}

struct VM {
    vars: HashMap<String, Value>,
    lines: Vec<String>,
    ip: usize,
    labels: HashMap<String, usize>,
    functions: HashMap<String, (usize, usize, Vec<String>)>,
    skip_to: HashMap<usize, usize>,
    block_info: Vec<BlockInfo>,
    if_stack: Vec<usize>,
    loop_stack: Vec<LoopFrame>,
    try_stack: Vec<usize>,
    switch_stack: Vec<(usize, Value, bool)>,
}

impl VM {
    fn new(lines: Vec<String>) -> Self {
        Self {
            vars: HashMap::new(),
            lines,
            ip: 0,
            labels: HashMap::new(),
            functions: HashMap::new(),
            skip_to: HashMap::new(),
            block_info: Vec::new(),
            if_stack: Vec::new(),
            loop_stack: Vec::new(),
            try_stack: Vec::new(),
            switch_stack: Vec::new(),
        }
    }

    fn precompute(&mut self) {
        self.block_info = vec![
            BlockInfo {
                matching_else: None,
                matching_end: None
            };
            self.lines.len()
        ];
        let mut stack: Vec<(usize, char)> = Vec::new();

        for (i, line) in self.lines.iter().enumerate() {
            let cmd = get_command_char(line);
            match cmd {
                '?' => {
                    let is_cond_jump = if let Ok(stmt) = parse_statement(line) {
                        matches!(stmt, Statement::CondJump(_, _))
                    } else {
                        false
                    };
                    if !is_cond_jump {
                        stack.push((i, '?'));
                    }
                }
                ':' => {
                    for j in (0..stack.len()).rev() {
                        if stack[j].1 == '?'
                            && self.block_info[stack[j].0].matching_else.is_none()
                        {
                            self.block_info[stack[j].0].matching_else = Some(i);
                            break;
                        }
                    }
                }
                ';' => {
                    for j in (0..stack.len()).rev() {
                        if stack[j].1 == '?' {
                            let start = stack.remove(j).0;
                            self.block_info[start].matching_end = Some(i);
                            break;
                        }
                    }
                }
                '@' | 'O' => stack.push((i, cmd)),
                '&' => {
                    for j in (0..stack.len()).rev() {
                        if stack[j].1 == '@' || stack[j].1 == 'O' {
                            let start = stack.remove(j).0;
                            self.block_info[start].matching_end = Some(i);
                            self.block_info[i].matching_end = Some(start);
                            break;
                        }
                    }
                }
                'T' => stack.push((i, 'T')),
                'C' => {
                    for j in (0..stack.len()).rev() {
                        if stack[j].1 == 'T'
                            && self.block_info[stack[j].0].matching_else.is_none()
                        {
                            self.block_info[stack[j].0].matching_else = Some(i);
                            break;
                        }
                    }
                }
                'Y' => {
                    for j in (0..stack.len()).rev() {
                        if stack[j].1 == 'T' {
                            let start = stack.remove(j).0;
                            self.block_info[start].matching_end = Some(i);
                            break;
                        }
                    }
                }
                'W' => stack.push((i, 'W')),
                'Z' => {
                    for j in (0..stack.len()).rev() {
                        if stack[j].1 == 'W' {
                            let start = stack.remove(j).0;
                            self.block_info[start].matching_end = Some(i);
                            break;
                        }
                    }
                }
                'L' => {
                    if let Ok(Statement::Label(name)) = parse_statement(line) {
                        self.labels.insert(name, i);
                    }
                }
                _ => {}
            }
        }

        let mut i = 0;
        while i < self.lines.len() {
            if let Ok(Statement::FunctionDef(name, params)) = parse_statement(&self.lines[i]) {
                let mut end = i + 1;
                let mut depth = 1;
                while end < self.lines.len() {
                    let cmd = get_command_char(&self.lines[end]);
                    if cmd == 'F' {
                        depth += 1;
                    } else if cmd == '~' {
                        depth -= 1;
                        if depth == 0 {
                            break;
                        }
                    }
                    end += 1;
                }
                self.functions.insert(name, (i, end, params));
                self.skip_to.insert(i, end + 1);
                i = end + 1;
                continue;
            }
            i += 1;
        }
    }

    fn eval_binop(&self, l: &Value, op: &str, r: &Value) -> Result<Value, VMError> {
        match (l, r) {
            (Value::Int(a), Value::Int(b)) => match op {
                "+" => Ok(Value::Int(a + b)),
                "-" => Ok(Value::Int(a - b)),
                "*" => Ok(Value::Int(a * b)),
                "/" => {
                    if *b == 0 {
                        return Err(VMError::Str("division by zero".into()));
                    }
                    Ok(Value::Int(a / b))
                }
                "=" => Ok(Value::Bool(a == b)),
                "!=" => Ok(Value::Bool(a != b)),
                "<" => Ok(Value::Bool(a < b)),
                ">" => Ok(Value::Bool(a > b)),
                "<=" => Ok(Value::Bool(a <= b)),
                ">=" => Ok(Value::Bool(a >= b)),
                _ => Err(VMError::Str(format!("Unknown op {}", op))),
            },
            (Value::Float(a), Value::Float(b)) => match op {
                "+" => Ok(Value::Float(a + b)),
                "-" => Ok(Value::Float(a - b)),
                "*" => Ok(Value::Float(a * b)),
                "/" => {
                    if *b == 0.0 {
                        return Err(VMError::Str("division by zero".into()));
                    }
                    Ok(Value::Float(a / b))
                }
                "=" => Ok(Value::Bool(a == b)),
                "!=" => Ok(Value::Bool(a != b)),
                "<" => Ok(Value::Bool(a < b)),
                ">" => Ok(Value::Bool(a > b)),
                "<=" => Ok(Value::Bool(a <= b)),
                ">=" => Ok(Value::Bool(a >= b)),
                _ => Err(VMError::Str(format!("Unknown op {}", op))),
            },
            (Value::Int(a), Value::Float(_)) => {
                self.eval_binop(&Value::Float(*a as f64), op, r)
            }
            (Value::Float(_), Value::Int(b)) => {
                self.eval_binop(l, op, &Value::Float(*b as f64))
            }
            (Value::Str(a), Value::Str(b)) => match op {
                "+" => Ok(Value::Str(format!("{}{}", a, b))),
                "=" => Ok(Value::Bool(a == b)),
                "!=" => Ok(Value::Bool(a != b)),
                _ => Err(VMError::Str("Invalid string op".into())),
            },
            (Value::Str(a), Value::Int(b)) if op == "+" => {
                Ok(Value::Str(format!("{}{}", a, b)))
            }
            (Value::Int(a), Value::Str(b)) if op == "+" => {
                Ok(Value::Str(format!("{}{}", a, b)))
            }
            _ => Err(VMError::Str(format!("Unsupported types for op {}", op))),
        }
    }

    fn eval_module_method(
        &self,
        mod_name: &str,
        method: &str,
        args: Vec<Value>,
    ) -> Result<Value, VMError> {
        match mod_name {
            "math" => match method {
                "sqrt" => {
                    if args.len() != 1 {
                        return Err(VMError::Str("math.sqrt takes 1 argument".into()));
                    }
                    match &args[0] {
                        Value::Int(i) => Ok(Value::Float((*i as f64).sqrt())),
                        Value::Float(f) => Ok(Value::Float(f.sqrt())),
                        _ => Err(VMError::Str("math.sqrt requires a number".into())),
                    }
                }
                "pi" => Ok(Value::Float(std::f64::consts::PI)),
                "e" => Ok(Value::Float(std::f64::consts::E)),
                "floor" => {
                    if args.len() != 1 {
                        return Err(VMError::Str("math.floor takes 1 argument".into()));
                    }
                    match &args[0] {
                        Value::Int(i) => Ok(Value::Int(*i)),
                        Value::Float(f) => Ok(Value::Int(f.floor() as i64)),
                        _ => Err(VMError::Str("math.floor requires a number".into())),
                    }
                }
                "ceil" => {
                    if args.len() != 1 {
                        return Err(VMError::Str("math.ceil takes 1 argument".into()));
                    }
                    match &args[0] {
                        Value::Int(i) => Ok(Value::Int(*i)),
                        Value::Float(f) => Ok(Value::Int(f.ceil() as i64)),
                        _ => Err(VMError::Str("math.ceil requires a number".into())),
                    }
                }
                _ => Err(VMError::Str(format!("Unknown math method {}", method))),
            },
            "os" => match method {
                "name" => {
                    if cfg!(target_os = "windows") {
                        Ok(Value::Str("nt".into()))
                    } else {
                        Ok(Value::Str("posix".into()))
                    }
                }
                "getcwd" => {
                    let cwd = std::env::current_dir().unwrap_or_default();
                    Ok(Value::Str(cwd.to_string_lossy().into_owned()))
                }
                "system" => {
                    if args.len() != 1 {
                        return Err(VMError::Str("os.system takes 1 argument".into()));
                    }
                    if let Value::Str(cmd) = &args[0] {
                        let status = if cfg!(target_os = "windows") {
                            std::process::Command::new("cmd")
                                .args(["/C", cmd])
                                .status()
                        } else {
                            std::process::Command::new("sh")
                                .args(["-c", cmd])
                                .status()
                        };
                        match status {
                            Ok(s) => Ok(Value::Int(s.code().unwrap_or(0) as i64)),
                            Err(e) => {
                                Err(VMError::Str(format!("os.system failed: {}", e)))
                            }
                        }
                    } else {
                        Err(VMError::Str("os.system requires a string".into()))
                    }
                }
                _ => Err(VMError::Str(format!("Unknown os method {}", method))),
            },
            "random" => match method {
                "randint" => {
                    if args.len() != 2 {
                        return Err(VMError::Str(
                            "random.randint takes 2 arguments".into(),
                        ));
                    }
                    let a = match &args[0] {
                        Value::Int(i) => *i,
                        _ => return Err(VMError::Str("randint requires ints".into())),
                    };
                    let b = match &args[1] {
                        Value::Int(i) => *i,
                        _ => return Err(VMError::Str("randint requires ints".into())),
                    };
                    let range = (b - a).unsigned_abs() + 1;
                    let res = a + (next_random() % range) as i64;
                    Ok(Value::Int(res))
                }
                _ => Err(VMError::Str(format!(
                    "Unknown random method {}",
                    method
                ))),
            },
            _ => Err(VMError::Str(format!(
                "Unknown module: {}",
                mod_name
            ))),
        }
    }

    fn eval_method_with_args(
        &self,
        obj: Value,
        method: &str,
        args: Vec<Value>,
    ) -> Result<Value, VMError> {
        match obj {
            Value::Str(s) => {
                if !args.is_empty() {
                    return Err(VMError::Str(
                        "String methods take no arguments".into(),
                    ));
                }
                match method {
                    "U" => Ok(Value::Str(s.to_uppercase())),
                    "L" => Ok(Value::Str(s.to_lowercase())),
                    "S" => Ok(Value::Str(s.trim().to_string())),
                    "T" => Ok(Value::Str(
                        s.split_whitespace()
                            .map(|w| {
                                let mut c = w.chars();
                                match c.next() {
                                    None => String::new(),
                                    Some(f) => {
                                        f.to_uppercase().collect::<String>() + c.as_str()
                                    }
                                }
                            })
                            .collect::<Vec<_>>()
                            .join(" "),
                    )),
                    "C" => {
                        let mut c = s.chars();
                        Ok(Value::Str(match c.next() {
                            None => String::new(),
                            Some(f) => {
                                f.to_uppercase().collect::<String>() + c.as_str()
                            }
                        }))
                    }
                    _ => Err(VMError::Str(format!(
                        "Unknown string method {}",
                        method
                    ))),
                }
            }
            Value::Module(mod_name) => self.eval_module_method(&mod_name, method, args),
            _ => Err(VMError::Str(
                "Methods only on strings or modules".into(),
            )),
        }
    }

    fn eval_expr(&mut self, expr: &Expr) -> Result<Value, VMError> {
        match expr {
            Expr::Num(n) => {
                if *n == (*n as i64) as f64 {
                    Ok(Value::Int(*n as i64))
                } else {
                    Ok(Value::Float(*n))
                }
            }
            Expr::Str(s) => Ok(Value::Str(s.clone())),
            Expr::Var(v) => Ok(self.vars.get(v).cloned().unwrap_or(Value::None)),
            Expr::BinOp(left, op, right) => {
                let l = self.eval_expr(left)?;
                let r = self.eval_expr(right)?;
                self.eval_binop(&l, op, &r)
            }
            Expr::UnaryOp(op, right) => {
                let r = self.eval_expr(right)?;
                match op.as_str() {
                    "-" => match r {
                        Value::Int(i) => Ok(Value::Int(-i)),
                        Value::Float(f) => Ok(Value::Float(-f)),
                        _ => Err(VMError::Str("Unary - on non-number".into())),
                    },
                    "!" => match r {
                        Value::Bool(b) => Ok(Value::Bool(!b)),
                        _ => Err(VMError::Str("Unary ! on non-bool".into())),
                    },
                    _ => Err(VMError::Str("Unknown unary".into())),
                }
            }
            Expr::FuncCall(name, args) => {
                let mut eval_args = Vec::new();
                for a in args {
                    eval_args.push(self.eval_expr(a)?);
                }
                self.execute_function(name, eval_args)
            }
            Expr::MethodCall(obj, method) => {
                let o = self.eval_expr(obj)?;
                self.eval_method_with_args(o, method, vec![])
            }
            Expr::MethodCallWithArgs(obj, method, args) => {
                let o = self.eval_expr(obj)?;
                let mut eval_args = Vec::new();
                for a in args {
                    eval_args.push(self.eval_expr(a)?);
                }
                self.eval_method_with_args(o, method, eval_args)
            }
        }
    }

    fn execute_python(&mut self, code: &str) -> Result<(), VMError> {
        let temp_dir = std::env::temp_dir();
        let script_path = temp_dir.join("vulpin_exec.py");
        let vars_path = temp_dir.join("vulpin_vars_out.txt");

        let mut script = String::new();
        script.push_str("import sys, os\n\n");

        for (k, v) in &self.vars {
            let py_val = match v {
                Value::Int(i) => i.to_string(),
                Value::Float(f) => f.to_string(),
                Value::Str(s) => {
                    let escaped = s
                        .replace('\\', "\\\\")
                        .replace('"', "\\\"")
                        .replace('\n', "\\n")
                        .replace('\r', "\\r")
                        .replace('\t', "\\t");
                    format!("\"{}\"", escaped)
                }
                Value::Bool(b) => {
                    if *b {
                        "True".to_string()
                    } else {
                        "False".to_string()
                    }
                }
                Value::None => "None".to_string(),
                Value::Module(m) => format!("\"{}\"", m),
            };
            script.push_str(&format!("{} = {}\n", k, py_val));
        }
        script.push('\n');

        script.push_str(code);
        script.push('\n');

        let vars_path_str = vars_path.display().to_string().replace('\\', "\\\\");
        script.push_str(&format!(
            "\n__vf = open('{}', 'w', encoding='utf-8')\n",
            vars_path_str
        ));
        script.push_str("for __k, __v in list(locals().items()):\n");
        script.push_str("    if not __k.startswith('_'):\n");
        script.push_str("        try:\n");
        script.push_str(
            "            __vf.write(__k + '=' + type(__v).__name__ + ':' + repr(__v) + '\\n')\n",
        );
        script.push_str("        except:\n");
        script.push_str("            pass\n");
        script.push_str("__vf.close()\n");

        std::fs::write(&script_path, &script)
            .map_err(|e| VMError::Str(format!("Failed to write temp script: {}", e)))?;

        let python_cmd = if cfg!(target_os = "windows") {
            "python"
        } else {
            "python3"
        };

        let output = std::process::Command::new(python_cmd)
            .arg(&script_path)
            .output()
            .map_err(|e| VMError::Str(format!("Failed to execute Python (is Python installed?): {}", e)))?;

        let stdout = String::from_utf8_lossy(&output.stdout);
        let stderr = String::from_utf8_lossy(&output.stderr);

        if !stdout.is_empty() {
            print!("{}", stdout);
            io::stdout().flush().unwrap();
        }
        if !stderr.is_empty() {
            eprint!("{}", stderr);
        }

        if !output.status.success() && stderr.is_empty() {
            return Err(VMError::Str(format!(
                "Python exited with code {}",
                output.status.code().unwrap_or(-1)
            )));
        }

        if let Ok(var_content) = std::fs::read_to_string(&vars_path) {
            for line in var_content.lines() {
                if let Some(eq_pos) = line.find('=') {
                    let key = &line[..eq_pos];
                    let rest = &line[eq_pos + 1..];
                    if let Some(colon_pos) = rest.find(':') {
                        let type_name = &rest[..colon_pos];
                        let val_str = &rest[colon_pos + 1..];
                        let val = match type_name {
                            "int" => Value::Int(val_str.parse().unwrap_or(0)),
                            "float" => Value::Float(val_str.parse().unwrap_or(0.0)),
                            "str" => {
                                let s = val_str.trim();
                                if (s.starts_with('\'') && s.ends_with('\''))
                                    || (s.starts_with('"') && s.ends_with('"'))
                                {
                                    let inner = &s[1..s.len() - 1];
                                    let unescaped = inner
                                        .replace("\\n", "\n")
                                        .replace("\\r", "\r")
                                        .replace("\\t", "\t")
                                        .replace("\\'", "'")
                                        .replace("\\\"", "\"")
                                        .replace("\\\\", "\\");
                                    Value::Str(unescaped)
                                } else {
                                    Value::Str(s.to_string())
                                }
                            }
                            "bool" => Value::Bool(val_str == "True"),
                            "NoneType" => Value::None,
                            // Ignore classes, functions, and complex objects to prevent them from becoming strings
                            _ => continue,
                        };
                        self.vars.insert(key.to_string(), val);
                    }
                }
            }
        }

        let _ = std::fs::remove_file(&script_path);
        let _ = std::fs::remove_file(&vars_path);

        Ok(())
    }

    fn execute_rust(&self, code: &str) -> Result<(), VMError> {
        let temp_dir = std::env::temp_dir();
        let script_path = temp_dir.join("vulpin_rust_exec.rs");
        let exe_path = temp_dir.join(if cfg!(target_os = "windows") { "vulpin_rust_exec.exe" } else { "vulpin_rust_exec" });

        let final_code = if code.contains("fn main") {
            code.to_string()
        } else {
            format!("fn main() {{\n{}\n}}", code)
        };

        std::fs::write(&script_path, final_code)
            .map_err(|e| VMError::Str(format!("Failed to write temp Rust script: {}", e)))?;

        let status = std::process::Command::new("rustc")
            .arg(&script_path)
            .arg("-o")
            .arg(&exe_path)
            .status()
            .map_err(|e| VMError::Str(format!("Failed to run rustc (is Rust installed?): {}", e)))?;

        if !status.success() {
            return Err(VMError::Str("Rust compilation failed. Check your syntax.".into()));
        }

        let output = std::process::Command::new(&exe_path)
            .output()
            .map_err(|e| VMError::Str(format!("Failed to execute Rust binary: {}", e)))?;

        let stdout = String::from_utf8_lossy(&output.stdout);
        let stderr = String::from_utf8_lossy(&output.stderr);

        if !stdout.is_empty() {
            print!("{}", stdout);
            io::stdout().flush().unwrap();
        }
        if !stderr.is_empty() {
            eprint!("{}", stderr);
        }

        let _ = std::fs::remove_file(&script_path);
        let _ = std::fs::remove_file(&exe_path);

        Ok(())
    }

    fn execute_statement(&mut self, stmt: Statement) -> Result<(), VMError> {
        match stmt {
            Statement::PrintNewline(expr) => {
                let val = self.eval_expr(&expr)?;
                println!("{}", value_to_string(&val));
            }
            Statement::Print(expr) => {
                let val = self.eval_expr(&expr)?;
                print!("{}", value_to_string(&val));
                io::stdout().flush().unwrap();
            }
            Statement::ExprStmt(expr) => {
                let _ = self.eval_expr(&expr)?;
            }
            Statement::Assign(var, expr) => {
                let val = self.eval_expr(&expr)?;
                self.vars.insert(var, val);
            }
            Statement::ArithAssign(var, op, expr) => {
                let current = self.vars.get(&var).cloned().unwrap_or(Value::Int(0));
                let right = self.eval_expr(&expr)?;
                let new_val = self.eval_binop(&current, &op, &right)?;
                self.vars.insert(var, new_val);
            }
            Statement::StrReplace(var, old_expr, new_expr) => {
                let s = self
                    .vars
                    .get(&var)
                    .cloned()
                    .unwrap_or(Value::Str("".into()));
                let old = self.eval_expr(&old_expr)?;
                let new = self.eval_expr(&new_expr)?;
                if let (Value::Str(mut s), Value::Str(old), Value::Str(new)) = (s, old, new) {
                    s = s.replace(&old, &new);
                    self.vars.insert(var, Value::Str(s));
                }
            }
            Statement::Delay(expr) => {
                let val = self.eval_expr(&expr)?;
                let ms = match val {
                    Value::Int(i) => (i as f64 * 1000.0) as u64,
                    Value::Float(f) => (f * 1000.0) as u64,
                    _ => 0,
                };
                std::thread::sleep(std::time::Duration::from_millis(ms));
            }
            Statement::Delete(var) => {
                self.vars.remove(&var);
            }
            Statement::Input(var, prompt, type_char) => {
                print!("{}", prompt);
                io::stdout().flush().unwrap();
                let mut input = String::new();
                io::stdin().read_line(&mut input).unwrap();
                let input = input.trim();
                let val = match type_char.as_str() {
                    "I" => Value::Int(input.parse().unwrap_or(0)),
                    "F" => Value::Float(input.parse().unwrap_or(0.0)),
                    "N" => {
                        if let Ok(i) = input.parse::<i64>() {
                            Value::Int(i)
                        } else if let Ok(f) = input.parse::<f64>() {
                            Value::Float(f)
                        } else {
                            Value::Int(0)
                        }
                    }
                    "L" => Value::Str(
                        input
                            .chars()
                            .next()
                            .map(|c| c.to_string())
                            .unwrap_or_default(),
                    ),
                    _ => Value::Str(input.to_string()),
                };
                self.vars.insert(var, val);
            }
            Statement::Quit => std::process::exit(0),
            Statement::ErrorExit(expr) => {
                let val = self.eval_expr(&expr)?;
                eprintln!("Error: {}", value_to_string(&val));
                std::process::exit(1);
            }
            Statement::Import(file) => {
                if file.ends_with(".vul") {
                    if let Ok(content) = std::fs::read_to_string(&file) {
                        let lines: Vec<String> =
                            content.lines().map(|s| s.to_string()).collect();
                        let mut sub_vm = VM::new(lines);
                        sub_vm.precompute();
                        let _ = sub_vm.run();
                        self.vars.extend(sub_vm.vars);
                    }
                } else {
                    self.vars.insert(file.clone(), Value::Module(file));
                }
            }
            Statement::If(expr) => {
                let cond = self.eval_expr(&expr)?;
                if cond.as_bool() {
                    self.if_stack.push(self.ip - 1);
                } else {
                    let info = &self.block_info[self.ip - 1];
                    let target = info
                        .matching_else
                        .unwrap_or_else(|| info.matching_end.unwrap());
                    self.ip = target + 1;
                }
            }
            Statement::CondJump(expr, label) => {
                let cond = self.eval_expr(&expr)?;
                if cond.as_bool() {
                    let target = self
                        .labels
                        .get(&label)
                        .cloned()
                        .ok_or_else(|| VMError::Str(format!("Undefined label {}", label)))?;
                    self.ip = target + 1;
                }
            }
            Statement::Else => {
                let start_ip = self.if_stack.pop().unwrap();
                let end_ip = self.block_info[start_ip].matching_end.unwrap();
                self.ip = end_ip + 1;
            }
            Statement::EndIf => {
                if !self.if_stack.is_empty()
                    && self.block_info[*self.if_stack.last().unwrap()].matching_end
                    == Some(self.ip - 1)
                {
                    self.if_stack.pop();
                }
            }
            Statement::While(expr) => {
                let cond = self.eval_expr(&expr)?;
                if cond.as_bool() {
                    self.loop_stack.push(LoopFrame::While(self.ip - 1));
                } else {
                    let end_ip = self.block_info[self.ip - 1].matching_end.unwrap();
                    self.ip = end_ip + 1;
                }
            }
            Statement::EndLoop => {
                let frame = self.loop_stack.pop().unwrap();
                match frame {
                    LoopFrame::While(start_ip) => self.ip = start_ip,
                    LoopFrame::For(start_ip, var, end, step) => {
                        let current =
                            self.vars.get(&var).cloned().unwrap_or(Value::Int(0));
                        let next = self.eval_binop(&current, "+", &step)?;
                        self.vars.insert(var.clone(), next.clone());
                        if self.in_range(&next, &end, &step) {
                            self.loop_stack
                                .push(LoopFrame::For(start_ip, var, end, step));
                            self.ip = start_ip + 1;
                        } else {
                            let end_ip =
                                self.block_info[start_ip].matching_end.unwrap();
                            self.ip = end_ip + 1;
                        }
                    }
                }
            }
            Statement::ForLoop(var, start_expr, end_expr, step_expr) => {
                let start = self.eval_expr(&start_expr)?;
                let end = self.eval_expr(&end_expr)?;
                let step = step_expr
                    .map(|e| self.eval_expr(&e))
                    .unwrap_or(Ok(Value::Int(1)))?;
                self.vars.insert(var.clone(), start.clone());
                if self.in_range(&start, &end, &step) {
                    self.loop_stack
                        .push(LoopFrame::For(self.ip - 1, var, end, step));
                } else {
                    let end_ip = self.block_info[self.ip - 1].matching_end.unwrap();
                    self.ip = end_ip + 1;
                }
            }
            Statement::Jump(label) => {
                let target = self
                    .labels
                    .get(&label)
                    .cloned()
                    .ok_or_else(|| VMError::Str(format!("Undefined label {}", label)))?;
                self.ip = target + 1;
            }
            Statement::Return(expr) => {
                let val = self.eval_expr(&expr)?;
                return Err(VMError::Return(val));
            }
            Statement::Try => {
                self.try_stack.push(self.ip - 1);
            }
            Statement::Catch(_) => {
                let t_ip = self.try_stack.pop().unwrap();
                let y_ip = self.block_info[t_ip].matching_end.unwrap();
                self.ip = y_ip + 1;
            }
            Statement::Switch(expr) => {
                let val = self.eval_expr(&expr)?;
                self.switch_stack.push((self.ip - 1, val, false));
            }
            Statement::Case(expr) => {
                let start_ip = self.switch_stack.last().unwrap().0;
                let matched = self.switch_stack.last().unwrap().2;

                if matched {
                    let end_ip = self.block_info[start_ip].matching_end.unwrap();
                    self.ip = end_ip;
                    return Ok(());
                }

                let switch_val = self.switch_stack.last().unwrap().1.clone();
                let case_val = self.eval_expr(&expr)?;
                let is_match = self
                    .eval_binop(&switch_val, "=", &case_val)
                    .unwrap_or(Value::Bool(false))
                    .as_bool();

                if is_match {
                    self.switch_stack.last_mut().unwrap().2 = true;
                } else {
                    let mut skip_depth = 0;
                    let mut target = self.ip;
                    while target < self.lines.len() {
                        let next_cmd = get_command_char(&self.lines[target]);
                        if next_cmd == 'V' || next_cmd == 'N' {
                            if skip_depth == 0 {
                                break;
                            }
                        } else if next_cmd == 'W' {
                            skip_depth += 1;
                        } else if next_cmd == 'Z' {
                            if skip_depth == 0 {
                                break;
                            }
                            skip_depth -= 1;
                        }
                        target += 1;
                    }
                    self.ip = target;
                }
            }
            Statement::Default => {
                let start_ip = self.switch_stack.last().unwrap().0;
                let matched = self.switch_stack.last().unwrap().2;
                if matched {
                    let end_ip = self.block_info[start_ip].matching_end.unwrap();
                    self.ip = end_ip;
                } else {
                    self.switch_stack.last_mut().unwrap().2 = true;
                }
            }
            Statement::EndSwitch => {
                self.switch_stack.pop();
            }
            Statement::PythonExec(code) => {
                let mut full_code = code.clone();
                // Group consecutive ! lines (and empty lines between them)
                while self.ip < self.lines.len() {
                    let next_line = &self.lines[self.ip];
                    let stripped = strip_comment(next_line);
                    let trimmed = stripped.trim();

                    if trimmed.is_empty() {
                        let mut has_more_python = false;
                        for j in (self.ip + 1)..self.lines.len() {
                            let peek_trimmed = strip_comment(&self.lines[j]).trim().to_string();
                            if peek_trimmed.is_empty() { continue; }
                            if peek_trimmed.starts_with('!') { has_more_python = true; }
                            break;
                        }
                        if has_more_python {
                            full_code.push('\n');
                            self.ip += 1;
                            continue;
                        } else {
                            break;
                        }
                    } else if trimmed.starts_with('!') {
                        full_code.push('\n');
                        full_code.push_str(trimmed.strip_prefix('!').unwrap_or(""));
                        self.ip += 1;
                    } else {
                        break;
                    }
                }
                self.execute_python(&full_code)?;
            }
            Statement::RustExec(code) => {
                let mut full_code = code.clone();
                while self.ip < self.lines.len() {
                    let next_line = &self.lines[self.ip];
                    let stripped = strip_comment(next_line);
                    let trimmed = stripped.trim();
                    if trimmed.is_empty() {
                        let mut has_more_rust = false;
                        for j in (self.ip + 1)..self.lines.len() {
                            let peek_trimmed = strip_comment(&self.lines[j]).trim().to_string();
                            if peek_trimmed.is_empty() { continue; }
                            if peek_trimmed.starts_with('^') { has_more_rust = true; }
                            break;
                        }
                        if has_more_rust {
                            full_code.push('\n');
                            self.ip += 1;
                            continue;
                        } else {
                            break;
                        }
                    } else if trimmed.starts_with('^') {
                        full_code.push('\n');
                        full_code.push_str(trimmed.strip_prefix('^').unwrap_or(""));
                        self.ip += 1;
                    } else {
                        break;
                    }
                }
                self.execute_rust(&full_code)?;
            }
            _ => {}
        }
        Ok(())
    }

    fn in_range(&self, current: &Value, end: &Value, step: &Value) -> bool {
        match (current, end, step) {
            (Value::Int(c), Value::Int(e), Value::Int(s)) => {
                if *s > 0 {
                    c < e
                } else {
                    c > e
                }
            }
            (Value::Float(c), Value::Float(e), Value::Float(s)) => {
                if *s > 0.0 {
                    c < e
                } else {
                    c > e
                }
            }
            _ => false,
        }
    }

    fn parse_c_var(line: &str) -> Option<String> {
        let rest = line.trim().strip_prefix('C')?.trim();
        if rest.is_empty() {
            return None;
        }
        let tokens = tokenize_expr(rest).ok()?;
        if let Some(Token::Str(s)) = tokens.first() {
            Some(s.clone())
        } else {
            None
        }
    }

    fn execute_function(
        &mut self,
        name: &str,
        args: Vec<Value>,
    ) -> Result<Value, VMError> {
        let (start_ip, end_ip, params) = self
            .functions
            .get(name)
            .cloned()
            .ok_or_else(|| VMError::Str(format!("Undefined function {}", name)))?;
        if args.len() != params.len() {
            return Err(VMError::Str(format!(
                "Arg count mismatch for {}",
                name
            )));
        }

        let saved_vars = self.vars.clone();
        let saved_ip = self.ip;
        let saved_if_stack = self.if_stack.clone();
        let saved_loop_stack = self.loop_stack.clone();
        let saved_try_stack = self.try_stack.clone();
        let saved_switch_stack = self.switch_stack.clone();

        for (p, v) in params.iter().zip(args) {
            self.vars.insert(p.clone(), v);
        }

        self.ip = start_ip + 1;
        let mut return_val = Value::None;

        while self.ip <= end_ip {
            let line = self.lines[self.ip].clone();
            self.ip += 1;
            let stmt = parse_statement(&line).map_err(VMError::Str)?;
            match self.execute_statement(stmt) {
                Ok(_) => {}
                Err(VMError::Return(val)) => {
                    return_val = val;
                    break;
                }
                Err(VMError::Str(e)) => {
                    if let Some(t_ip) = self.try_stack.pop() {
                        let c_ip = self.block_info[t_ip].matching_else.unwrap();
                        if let Some(var) = Self::parse_c_var(&self.lines[c_ip]) {
                            self.vars.insert(var, Value::Str(e.clone()));
                        }
                        self.ip = c_ip + 1;
                    } else {
                        self.vars = saved_vars;
                        self.ip = saved_ip;
                        self.if_stack = saved_if_stack;
                        self.loop_stack = saved_loop_stack;
                        self.try_stack = saved_try_stack;
                        self.switch_stack = saved_switch_stack;
                        return Err(VMError::Str(e));
                    }
                }
            }
        }

        self.vars = saved_vars;
        self.ip = saved_ip;
        self.if_stack = saved_if_stack;
        self.loop_stack = saved_loop_stack;
        self.try_stack = saved_try_stack;
        self.switch_stack = saved_switch_stack;

        Ok(return_val)
    }

    fn run(&mut self) -> Result<(), String> {
        while self.ip < self.lines.len() {
            if let Some(&skip_ip) = self.skip_to.get(&self.ip) {
                self.ip = skip_ip;
                continue;
            }

            let line = self.lines[self.ip].clone();
            self.ip += 1;
            let stmt = parse_statement(&line)
                .map_err(|e| format!("Parse error on line {}: {}", self.ip, e))?;

            match self.execute_statement(stmt) {
                Ok(_) => {}
                Err(VMError::Return(_)) => {
                    return Err("Return statement outside of function".into());
                }
                Err(VMError::Str(e)) => {
                    if let Some(t_ip) = self.try_stack.pop() {
                        let c_ip = self.block_info[t_ip].matching_else.unwrap();
                        if let Some(var) = Self::parse_c_var(&self.lines[c_ip]) {
                            self.vars.insert(var, Value::Str(e.clone()));
                        }
                        self.ip = c_ip + 1;
                    } else {
                        return Err(e);
                    }
                }
            }
        }
        Ok(())
    }
}

fn build_app(vul_file: &str, target_os: &str) {
    println!("--- Vulpin App Builder ---");
    let vul_content = match std::fs::read_to_string(vul_file) {
        Ok(c) => c,
        Err(e) => {
            eprintln!("Failed to read {}: {}", vul_file, e);
            return;
        }
    };

    let source_code = match std::fs::read_to_string("src/main.rs") {
        Ok(c) => c,
        Err(_) => {
            eprintln!("Error: Could not find src/main.rs. Please run the build command from the root of the Vulpin Rust project.");
            return;
        }
    };

    let main_idx = match source_code.rfind("fn main() {") {
        Some(i) => i,
        None => {
            eprintln!("Error: Could not find 'fn main()' in src/main.rs");
            return;
        }
    };
    let base_code = &source_code[..main_idx];

    let escaped_vul = vul_content
        .replace('\\', "\\\\")
        .replace('"', "\\\"")
        .replace('\n', "\\n")
        .replace('\r', "\\r");

    let custom_main = format!(
        r#"fn main() {{
    let script = "{}";
    let lines: Vec<String> = script.lines().map(|s| s.to_string()).collect();
    let mut vm = VM::new(lines);
    vm.precompute();
    if let Err(e) = vm.run() {{
        eprintln!("Unhandled Exception: {{}}", e);
    }}
}}"#,
        escaped_vul
    );

    let final_code = format!("{}{}", base_code, custom_main);

    let temp_dir = std::env::temp_dir().join("vulpin_build_temp");
    let _ = std::fs::remove_dir_all(&temp_dir);
    std::fs::create_dir_all(temp_dir.join("src")).unwrap();

    std::fs::write(temp_dir.join("Cargo.toml"), r#"
[package]
name = "Vulpin App"
version = "0.1.1"
edition = "2026"
"#).unwrap();

    std::fs::write(temp_dir.join("src/main.rs"), final_code).unwrap();

    let target_triple = match target_os {
        "windows" => "x86_64-pc-windows-gnu",
        "linux" => "x86_64-unknown-linux-gnu",
        "macos" => "x86_64-apple-darwin",
        _ => ""
    };

    let mut cmd = std::process::Command::new("cargo");
    cmd.arg("build").arg("--release");
    if !target_triple.is_empty() {
        cmd.arg("--target").arg(target_triple);
    }
    cmd.current_dir(&temp_dir);

    println!("Building standalone app for {}...", if target_os == "native" { "current OS" } else { target_os });
    let status = cmd.status().expect("Failed to run cargo. Is Rust installed?");

    if status.success() {
        let exe_name = if target_os == "windows" || (target_os == "native" && cfg!(target_os = "windows")) {
            "vulpin_app.exe"
        } else {
            "vulpin_app"
        };
        let mut output_path = temp_dir.join("target");
        if !target_triple.is_empty() {
            output_path = output_path.join(target_triple);
        }
        output_path = output_path.join("release").join(exe_name);

        let final_name = std::path::Path::new(vul_file).file_stem().unwrap_or_default().to_str().unwrap_or("app");
        let final_ext = if target_os == "windows" || (target_os == "native" && cfg!(target_os = "windows")) { ".exe" } else { "" };
        let dest = format!("{}{}", final_name, final_ext);

        std::fs::copy(&output_path, &dest).unwrap();
        println!("Successfully built: {}", dest);
    } else {
        eprintln!("Build failed.");
    }
}

fn main() {
    let args: Vec<String> = std::env::args().collect();

    if args.len() >= 3 && args[1] == "build" {
        let vul_file = &args[2];
        let mut target_os = "native";
        if args.len() >= 5 && args[3] == "--os" {
            target_os = &args[4];
        }
        if target_os == "all" {
            build_app(vul_file, "windows");
            build_app(vul_file, "linux");
            build_app(vul_file, "macos");
        } else {
            build_app(vul_file, target_os);
        }
        return;
    }

    let filename = if args.len() < 2 {
        if std::path::Path::new("app.vul").exists() {
            "app.vul"
        } else {
            eprintln!("Usage: vulpin <file.vul>");
            eprintln!("       vulpin build <file.vul> [--os <windows|linux|macos|all>]");
            return;
        }
    } else if args[1] == "version" {
        println!("Vul 0.7.5");
        return;
    } else {
        &args[1]
    };

    let content = match std::fs::read_to_string(filename) {
        Ok(c) => c,
        Err(e) => {
            eprintln!("Failed to read file: {}", e);
            return;
        }
    };

    let lines: Vec<String> = content.lines().map(|s| s.to_string()).collect();
    let mut vm = VM::new(lines);
    vm.precompute();

    if let Err(e) = vm.run() {
        eprintln!("Unhandled Exception: {}", e);
    }
}
