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

// === Error formatting helpers ===
fn err_red(msg: &str) -> String {
    format!("\x1b[1;31m{}\x1b[0m", msg)
}

fn err_yellow(msg: &str) -> String {
    format!("\x1b[1;33m{}\x1b[0m", msg)
}

fn err_cyan(msg: &str) -> String {
    format!("\x1b[1;36m{}\x1b[0m", msg)
}

fn format_error(line_num: usize, line_content: &str, msg: &str) -> String {
    format!(
        "{}\n  {} {}\n  {}│\n  {}│ {}\n  {}│ {}\n",
        err_red(&format!("Error [line {}]: {}", line_num, msg)),
        err_cyan("-->"),
        line_content.trim(),
        err_cyan(" "),
        err_cyan(" "),
        line_content.trim(),
        err_cyan(" "),
        err_yellow(&format!("^ {}", msg)),
    )
}

fn format_warning(line_num: usize, line_content: &str, msg: &str) -> String {
    format!(
        "{}\n  {} {}\n",
        err_yellow(&format!("Warning [line {}]: {}", line_num, msg)),
        err_cyan("-->"),
        line_content.trim(),
    )
}

#[derive(Debug, Clone, PartialEq)]
enum Value {
    Int(i64),
    Float(f64),
    Str(String),
    Bool(bool),
    Module(String),
    RustModule(String),
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
            Value::RustModule(_) => true,
            Value::None => false,
        }
    }

    fn type_name(&self) -> &'static str {
        match self {
            Value::Int(_) => "Int",
            Value::Float(_) => "Float",
            Value::Str(_) => "Str",
            Value::Bool(_) => "Bool",
            Value::Module(_) => "Module",
            Value::RustModule(_) => "RustModule",
            Value::None => "None",
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
        Value::RustModule(m) => format!("<rust::{}>", m),
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
    ColonColon,
    Percent,
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
            match num_s.parse() {
                Ok(n) => tokens.push(Token::Num(n)),
                Err(_) => return Err(format!("Invalid number literal: '{}'", num_s)),
            }
        } else if c == '"' {
            chars.next();
            let mut str_s = String::new();
            let mut closed = false;
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
                    closed = true;
                    break;
                } else {
                    str_s.push(d);
                    chars.next();
                }
            }
            if !closed {
                return Err(format!("Unterminated string literal: \"{}...", str_s));
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
            if var_s.is_empty() {
                return Err("Expected variable name after '$'".into());
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
        } else if c == ':' {
            chars.next();
            if let Some(&next_c) = chars.peek() {
                if next_c == ':' {
                    chars.next();
                    tokens.push(Token::ColonColon);
                } else {
                    return Err(format!(
                        "Unexpected ':' — did you mean '::' for Rust module path?"
                    ));
                }
            } else {
                return Err("Unexpected ':' at end of expression".into());
            }
        } else if c == '%' {
            chars.next();
            tokens.push(Token::Percent);
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
            return Err(format!(
                "Unexpected character '{}' in expression",
                c
            ));
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
    BoolLit(bool),
    NoneLit,
    BinOp(Box<Expr>, String, Box<Expr>),
    UnaryOp(String, Box<Expr>),
    FuncCall(String, Vec<Expr>),
    MethodCall(Box<Expr>, String),
    MethodCallWithArgs(Box<Expr>, String, Vec<Expr>),
    RustModuleCall(String, Vec<Expr>),
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
        let got = self.next();
        if got == t {
            Ok(())
        } else {
            Err(format!("Expected {:?}, got {:?}", t, got))
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

    fn parse_rust_module_path(&mut self, first_ident: String) -> Result<Expr, String> {
        let mut path = first_ident;
        while let Token::ColonColon = self.peek() {
            self.next();
            if let Token::Ident(segment) = self.next() {
                path = format!("{}::{}", path, segment);
            } else {
                return Err(format!(
                    "Expected identifier after '::' in Rust module path '{}::'",
                    path
                ));
            }
        }
        if let Token::LParen = self.peek() {
            self.next();
            let args = self.parse_arg_list()?;
            Ok(Expr::RustModuleCall(path, args))
        } else {
            Ok(Expr::Str(format!("%rust_mod%{}", path)))
        }
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
                if id == "None" {
                    Expr::NoneLit
                } else if id == "True" || id == "true" {
                    Expr::BoolLit(true)
                } else if id == "False" || id == "false" {
                    Expr::BoolLit(false)
                } else if let Token::ColonColon = self.peek() {
                    return self.parse_rust_module_path(id);
                } else if let Token::LParen = self.peek() {
                    self.next();
                    let args = self.parse_arg_list()?;
                    Expr::FuncCall(id, args)
                } else {
                    Expr::Str(id)
                }
            }
            Token::Percent => {
                if let Token::Ident(id) = self.next() {
                    return self.parse_rust_module_path(id);
                } else {
                    return Err(
                        "Expected Rust module path after '%' (e.g., % std::fs::write(...))"
                            .into(),
                    );
                }
            }
            Token::LParen => {
                let expr = self.parse()?;
                self.expect(Token::RParen)?;
                expr
            }
            Token::Eof => return Err("Unexpected end of expression".into()),
            other => return Err(format!("Unexpected token {:?} in expression", other)),
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
                return Err("Expected method name after '.'".into());
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
        match self.next() {
            Token::Str(s) => Ok(s),
            other => Err(format!(
                "Expected a quoted string \"...\", got {:?}",
                other
            )),
        }
    }
    fn parse_word(&mut self) -> Result<String, String> {
        match self.next() {
            Token::Ident(s) => Ok(s),
            Token::Op(s) => Ok(s),
            Token::Str(s) => Ok(s),
            other => Err(format!("Expected a word/operator, got {:?}", other)),
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
    RustModuleImport(String),
    RustModuleCall(Expr),
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
                        let expr = parse_expr_str(expr_str).map_err(|e| {
                            format!("In assignment '{} = ...': {}", lhs, e)
                        })?;
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
        'G' => Ok(Statement::PrintNewline(
            parse_expr_str(rest).map_err(|e| format!("G (print): {}", e))?,
        )),
        'P' => Ok(Statement::Print(
            parse_expr_str(rest).map_err(|e| format!("P (print-noln): {}", e))?,
        )),
        'A' => {
            if rest.is_empty() {
                return Err(
                    "A (arithmetic assign): missing arguments. Usage: A \"var\" \"op\" expr  (e.g., A \"x\" \"+\" 5)"
                        .into(),
                );
            }
            let mut p = ArgParser::new(rest)
                .map_err(|e| format!("A (arithmetic assign): {}", e))?;
            let var = p
                .parse_string()
                .map_err(|e| format!("A (arithmetic assign): expected variable name as quoted string. {}", e))?;
            let op = p
                .parse_word()
                .map_err(|e| format!("A (arithmetic assign): expected operator (+, -, *, /). {}", e))?;
            if !["+", "-", "*", "/"].contains(&op.as_str()) {
                return Err(format!(
                    "A (arithmetic assign): invalid operator '{}'. Must be one of: +, -, *, /",
                    op
                ));
            }
            let expr = p
                .parse_expr()
                .map_err(|e| format!("A (arithmetic assign): invalid expression. {}", e))?;
            Ok(Statement::ArithAssign(var, op, expr))
        }
        'S' => {
            if rest.is_empty() {
                return Err(
                    "S (string replace): missing arguments. Usage: S \"var\" \"old\" \"new\""
                        .into(),
                );
            }
            let mut p = ArgParser::new(rest)
                .map_err(|e| format!("S (string replace): {}", e))?;
            let var = p
                .parse_string()
                .map_err(|e| format!("S (string replace): expected variable name. {}", e))?;
            let old = p
                .parse_expr()
                .map_err(|e| format!("S (string replace): expected 'old' string. {}", e))?;
            let new = p
                .parse_expr()
                .map_err(|e| format!("S (string replace): expected 'new' string. {}", e))?;
            Ok(Statement::StrReplace(var, old, new))
        }
        'D' => {
            if rest.starts_with('"') {
                let mut p = ArgParser::new(rest)
                    .map_err(|e| format!("D (delete): {}", e))?;
                let var = p
                    .parse_string()
                    .map_err(|e| format!("D (delete): expected variable name. {}", e))?;
                Ok(Statement::Delete(var))
            } else if rest.is_empty() {
                return Err(
                    "D (delay/delete): missing argument. Usage: D seconds  or  D \"varname\""
                        .into(),
                );
            } else {
                Ok(Statement::Delay(
                    parse_expr_str(rest).map_err(|e| format!("D (delay): {}", e))?,
                ))
            }
        }
        'K' => {
            let mut p = ArgParser::new(rest)
                .map_err(|e| format!("K (input): {}", e))?;
            let var = p
                .parse_string()
                .map_err(|e| format!("K (input): expected variable name. {}", e))?;
            let prompt = p
                .parse_string()
                .map_err(|e| format!("K (input): expected prompt string. {}", e))?;
            let type_char = p.parse_string().unwrap_or_else(|_| "W".to_string());
            Ok(Statement::Input(var, prompt, type_char))
        }
        'Q' => Ok(Statement::Quit),
        'E' => Ok(Statement::ErrorExit(
            parse_expr_str(rest).map_err(|e| format!("E (error exit): {}", e))?,
        )),
        'U' => {
            if rest.is_empty() {
                return Err(
                    "U (import/use): missing argument. Usage: U \"module\" or U \"file.vul\""
                        .into(),
                );
            }
            let mut p = ArgParser::new(rest)
                .map_err(|e| format!("U (import): {}", e))?;
            let target = p
                .parse_string()
                .map_err(|e| format!("U (import): expected module/file name as quoted string. {}", e))?;
            Ok(Statement::Import(target))
        }
        '%' => {
            if rest.is_empty() {
                return Err(
                    "% (rust module): missing argument. Usage: % \"std::fs\" or % std::fs::write(...)".into(),
                );
            }
            if rest.starts_with('"') {
                let mut p = ArgParser::new(rest)
                    .map_err(|e| format!("% (rust module import): {}", e))?;
                let path = p.parse_string().map_err(|e| {
                    format!("% (rust module import): expected module path string. {}", e)
                })?;
                Ok(Statement::RustModuleImport(path))
            } else {
                let expr = parse_expr_str(rest)
                    .map_err(|e| format!("% (rust module call): {}", e))?;
                Ok(Statement::RustModuleCall(expr))
            }
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
                    parse_expr_str(cond_str)
                        .map_err(|e| format!("? (conditional jump): {}", e))?,
                    label.to_string(),
                ))
            } else {
                Ok(Statement::If(
                    parse_expr_str(rest).map_err(|e| format!("? (if): {}", e))?,
                ))
            }
        }
        ':' => Ok(Statement::Else),
        ';' => Ok(Statement::EndIf),
        '@' => Ok(Statement::While(
            parse_expr_str(rest).map_err(|e| format!("@ (while): {}", e))?,
        )),
        '&' => Ok(Statement::EndLoop),
        'O' => {
            let parts: Vec<&str> = rest.split_whitespace().collect();
            if parts.len() < 3 {
                return Err(format!(
                    "O (for loop): requires at least 3 args (var start end). Got: '{}'. Usage: O var start end [step]",
                    rest
                ));
            }
            let var = parts[0].to_string();
            let start = parse_expr_str(parts[1])
                .map_err(|e| format!("O (for loop): invalid start value: {}", e))?;
            let end = parse_expr_str(parts[2])
                .map_err(|e| format!("O (for loop): invalid end value: {}", e))?;
            let step = if parts.len() > 3 {
                Some(
                    parse_expr_str(parts[3])
                        .map_err(|e| format!("O (for loop): invalid step value: {}", e))?,
                )
            } else {
                None
            };
            Ok(Statement::ForLoop(var, start, end, step))
        }
        'L' => {
            let mut p = ArgParser::new(rest)
                .map_err(|e| format!("L (label): {}", e))?;
            Ok(Statement::Label(
                p.parse_word()
                    .map_err(|e| format!("L (label): expected label name. {}", e))?,
            ))
        }
        'J' => {
            let mut p = ArgParser::new(rest)
                .map_err(|e| format!("J (jump): {}", e))?;
            Ok(Statement::Jump(
                p.parse_word()
                    .map_err(|e| format!("J (jump): expected label name. {}", e))?,
            ))
        }
        'F' => {
            let mut p = ArgParser::new(rest)
                .map_err(|e| format!("F (function): {}", e))?;
            let name = p
                .parse_word()
                .map_err(|e| format!("F (function): expected function name. {}", e))?;
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
        'R' => Ok(Statement::Return(
            parse_expr_str(rest).map_err(|e| format!("R (return): {}", e))?,
        )),
        '~' => Ok(Statement::EndFunc),
        'T' => Ok(Statement::Try),
        'C' => {
            if rest.is_empty() {
                Ok(Statement::Catch(None))
            } else {
                let mut p = ArgParser::new(rest)
                    .map_err(|e| format!("C (catch): {}", e))?;
                Ok(Statement::Catch(Some(
                    p.parse_string()
                        .map_err(|e| format!("C (catch): expected error variable name. {}", e))?,
                )))
            }
        }
        'Y' => Ok(Statement::EndTry),
        'W' => Ok(Statement::Switch(
            parse_expr_str(rest).map_err(|e| format!("W (switch): {}", e))?,
        )),
        'V' => Ok(Statement::Case(
            parse_expr_str(rest).map_err(|e| format!("V (case): {}", e))?,
        )),
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
        '$' => Ok(Statement::ExprStmt(
            parse_expr_str(line).map_err(|e| format!("$ (expr): {}", e))?,
        )),
        _ => {
            if let Ok(expr) = parse_expr_str(line) {
                Ok(Statement::ExprStmt(expr))
            } else {
                Err(format!(
                    "Unknown command '{}'. Valid commands: G, P, A, S, D, K, Q, E, U, %, ?, :, ;, @, &, O, L, J, F, R, ~, T, C, Y, W, V, N, Z, !, ^, #, $",
                    cmd
                ))
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
    rust_modules: Vec<String>,
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
            rust_modules: Vec::new(),
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
        // Handle None comparisons
        match (l, r) {
            (Value::None, Value::None) => match op {
                "=" => return Ok(Value::Bool(true)),
                "!=" => return Ok(Value::Bool(false)),
                _ => {
                    return Err(VMError::Str(format!(
                        "Cannot apply operator '{}' to None. Only '=' and '!=' are supported for None comparisons.",
                        op
                    )))
                }
            },
            (Value::None, other) => match op {
                "=" => return Ok(Value::Bool(false)),
                "!=" => return Ok(Value::Bool(true)),
                _ => {
                    return Err(VMError::Str(format!(
                        "Cannot apply operator '{}' between None and {}. Only '=' and '!=' are supported.",
                        op,
                        other.type_name()
                    )))
                }
            },
            (other, Value::None) => match op {
                "=" => return Ok(Value::Bool(false)),
                "!=" => return Ok(Value::Bool(true)),
                _ => {
                    return Err(VMError::Str(format!(
                        "Cannot apply operator '{}' between {} and None. Only '=' and '!=' are supported.",
                        op,
                        other.type_name()
                    )))
                }
            },
            _ => {}
        }

        // Handle Bool comparisons
        if let (Value::Bool(a), Value::Bool(b)) = (l, r) {
            return match op {
                "=" => Ok(Value::Bool(a == b)),
                "!=" => Ok(Value::Bool(a != b)),
                _ => Err(VMError::Str(format!(
                    "Cannot apply operator '{}' to Bool values. Only '=' and '!=' are supported.",
                    op
                ))),
            };
        }

        match (l, r) {
            (Value::Int(a), Value::Int(b)) => match op {
                "+" => Ok(Value::Int(a + b)),
                "-" => Ok(Value::Int(a - b)),
                "*" => Ok(Value::Int(a * b)),
                "/" => {
                    if *b == 0 {
                        return Err(VMError::Str(
                            "Division by zero: attempted to divide integer by 0".into(),
                        ));
                    }
                    Ok(Value::Int(a / b))
                }
                "=" => Ok(Value::Bool(a == b)),
                "!=" => Ok(Value::Bool(a != b)),
                "<" => Ok(Value::Bool(a < b)),
                ">" => Ok(Value::Bool(a > b)),
                "<=" => Ok(Value::Bool(a <= b)),
                ">=" => Ok(Value::Bool(a >= b)),
                _ => Err(VMError::Str(format!("Unknown operator '{}'", op))),
            },
            (Value::Float(a), Value::Float(b)) => match op {
                "+" => Ok(Value::Float(a + b)),
                "-" => Ok(Value::Float(a - b)),
                "*" => Ok(Value::Float(a * b)),
                "/" => {
                    if *b == 0.0 {
                        return Err(VMError::Str(
                            "Division by zero: attempted to divide float by 0.0".into(),
                        ));
                    }
                    Ok(Value::Float(a / b))
                }
                "=" => Ok(Value::Bool(a == b)),
                "!=" => Ok(Value::Bool(a != b)),
                "<" => Ok(Value::Bool(a < b)),
                ">" => Ok(Value::Bool(a > b)),
                "<=" => Ok(Value::Bool(a <= b)),
                ">=" => Ok(Value::Bool(a >= b)),
                _ => Err(VMError::Str(format!("Unknown operator '{}'", op))),
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
                "<" => Ok(Value::Bool(a < b)),
                ">" => Ok(Value::Bool(a > b)),
                "<=" => Ok(Value::Bool(a <= b)),
                ">=" => Ok(Value::Bool(a >= b)),
                _ => Err(VMError::Str(format!(
                    "Cannot apply operator '{}' to strings. Supported: +, =, !=, <, >, <=, >=",
                    op
                ))),
            },
            (Value::Str(a), Value::Int(b)) if op == "+" => {
                Ok(Value::Str(format!("{}{}", a, b)))
            }
            (Value::Int(a), Value::Str(b)) if op == "+" => {
                Ok(Value::Str(format!("{}{}", a, b)))
            }
            (Value::Str(a), Value::Float(b)) if op == "+" => {
                Ok(Value::Str(format!("{}{}", a, b)))
            }
            (Value::Float(a), Value::Str(b)) if op == "+" => {
                Ok(Value::Str(format!("{}{}", a, b)))
            }
            (Value::Str(a), Value::Bool(b)) if op == "+" => {
                Ok(Value::Str(format!("{}{}", a, b)))
            }
            (Value::Bool(a), Value::Str(b)) if op == "+" => {
                Ok(Value::Str(format!("{}{}", a, b)))
            }
            (Value::Int(a), Value::Bool(b)) if op == "+" => {
                Ok(Value::Str(format!("{}{}", a, b)))
            }
            (Value::Bool(a), Value::Int(b)) if op == "+" => {
                Ok(Value::Str(format!("{}{}", a, b)))
            }
            (Value::Float(a), Value::Bool(b)) if op == "+" => {
                Ok(Value::Str(format!("{}{}", a, b)))
            }
            (Value::Bool(a), Value::Float(b)) if op == "+" => {
                Ok(Value::Str(format!("{}{}", a, b)))
            }
            _ => Err(VMError::Str(format!(
                "Type mismatch: cannot apply operator '{}' between {} ({}) and {} ({}). \
                 Hint: use std::convert::to_string() to convert values to strings for concatenation.",
                op,
                l.type_name(),
                value_to_string(l),
                r.type_name(),
                value_to_string(r),
            ))),
        }
    }

    fn eval_rust_module_call(
        &mut self,
        path: &str,
        args: Vec<Value>,
    ) -> Result<Value, VMError> {
        match path {
            "std::fs::read_to_string" => {
                if args.len() != 1 {
                    return Err(VMError::Str(format!(
                        "std::fs::read_to_string: expected 1 argument (path), got {}",
                        args.len()
                    )));
                }
                if let Value::Str(path) = &args[0] {
                    match std::fs::read_to_string(path) {
                        Ok(content) => Ok(Value::Str(content)),
                        Err(e) => Err(VMError::Str(format!(
                            "std::fs::read_to_string(\"{}\"): {}",
                            path, e
                        ))),
                    }
                } else {
                    Err(VMError::Str(format!(
                        "std::fs::read_to_string: argument must be a string path, got {}",
                        args[0].type_name()
                    )))
                }
            }
            "std::fs::write" => {
                if args.len() != 2 {
                    return Err(VMError::Str(format!(
                        "std::fs::write: expected 2 arguments (path, content), got {}",
                        args.len()
                    )));
                }
                if let (Value::Str(path), Value::Str(content)) = (&args[0], &args[1]) {
                    match std::fs::write(path, content) {
                        Ok(_) => Ok(Value::Bool(true)),
                        Err(e) => Err(VMError::Str(format!(
                            "std::fs::write(\"{}\"): {}",
                            path, e
                        ))),
                    }
                } else {
                    Err(VMError::Str(
                        "std::fs::write: both arguments must be strings (path, content)".into(),
                    ))
                }
            }
            "std::fs::append" => {
                if args.len() != 2 {
                    return Err(VMError::Str(format!(
                        "std::fs::append: expected 2 arguments (path, content), got {}",
                        args.len()
                    )));
                }
                if let (Value::Str(path), Value::Str(content)) = (&args[0], &args[1]) {
                    use std::io::Write as IoWrite;
                    match std::fs::OpenOptions::new().create(true).append(true).open(path) {
                        Ok(mut file) => match file.write_all(content.as_bytes()) {
                            Ok(_) => Ok(Value::Bool(true)),
                            Err(e) => Err(VMError::Str(format!(
                                "std::fs::append(\"{}\"): write error: {}",
                                path, e
                            ))),
                        },
                        Err(e) => Err(VMError::Str(format!(
                            "std::fs::append(\"{}\"): open error: {}",
                            path, e
                        ))),
                    }
                } else {
                    Err(VMError::Str(
                        "std::fs::append: both arguments must be strings (path, content)".into(),
                    ))
                }
            }
            "std::fs::remove_file" => {
                if args.len() != 1 {
                    return Err(VMError::Str(format!(
                        "std::fs::remove_file: expected 1 argument (path), got {}",
                        args.len()
                    )));
                }
                if let Value::Str(path) = &args[0] {
                    match std::fs::remove_file(path) {
                        Ok(_) => Ok(Value::Bool(true)),
                        Err(e) => Err(VMError::Str(format!(
                            "std::fs::remove_file(\"{}\"): {}",
                            path, e
                        ))),
                    }
                } else {
                    Err(VMError::Str(
                        "std::fs::remove_file: argument must be a string path".into(),
                    ))
                }
            }
            "std::fs::remove_dir" => {
                if args.len() != 1 {
                    return Err(VMError::Str(format!(
                        "std::fs::remove_dir: expected 1 argument (path), got {}",
                        args.len()
                    )));
                }
                if let Value::Str(path) = &args[0] {
                    match std::fs::remove_dir_all(path) {
                        Ok(_) => Ok(Value::Bool(true)),
                        Err(e) => Err(VMError::Str(format!(
                            "std::fs::remove_dir(\"{}\"): {}",
                            path, e
                        ))),
                    }
                } else {
                    Err(VMError::Str(
                        "std::fs::remove_dir: argument must be a string path".into(),
                    ))
                }
            }
            "std::fs::exists" => {
                if args.len() != 1 {
                    return Err(VMError::Str(format!(
                        "std::fs::exists: expected 1 argument (path), got {}",
                        args.len()
                    )));
                }
                if let Value::Str(path) = &args[0] {
                    Ok(Value::Bool(std::path::Path::new(path).exists()))
                } else {
                    Err(VMError::Str(
                        "std::fs::exists: argument must be a string path".into(),
                    ))
                }
            }
            "std::fs::is_file" => {
                if args.len() != 1 {
                    return Err(VMError::Str(format!(
                        "std::fs::is_file: expected 1 argument, got {}",
                        args.len()
                    )));
                }
                if let Value::Str(path) = &args[0] {
                    Ok(Value::Bool(std::path::Path::new(path).is_file()))
                } else {
                    Err(VMError::Str("std::fs::is_file: argument must be a string".into()))
                }
            }
            "std::fs::is_dir" => {
                if args.len() != 1 {
                    return Err(VMError::Str(format!(
                        "std::fs::is_dir: expected 1 argument, got {}",
                        args.len()
                    )));
                }
                if let Value::Str(path) = &args[0] {
                    Ok(Value::Bool(std::path::Path::new(path).is_dir()))
                } else {
                    Err(VMError::Str("std::fs::is_dir: argument must be a string".into()))
                }
            }
            "std::fs::create_dir" => {
                if args.len() != 1 {
                    return Err(VMError::Str(format!(
                        "std::fs::create_dir: expected 1 argument, got {}",
                        args.len()
                    )));
                }
                if let Value::Str(path) = &args[0] {
                    match std::fs::create_dir_all(path) {
                        Ok(_) => Ok(Value::Bool(true)),
                        Err(e) => Err(VMError::Str(format!(
                            "std::fs::create_dir(\"{}\"): {}",
                            path, e
                        ))),
                    }
                } else {
                    Err(VMError::Str(
                        "std::fs::create_dir: argument must be a string".into(),
                    ))
                }
            }
            "std::fs::read_dir" => {
                if args.len() != 1 {
                    return Err(VMError::Str(format!(
                        "std::fs::read_dir: expected 1 argument, got {}",
                        args.len()
                    )));
                }
                if let Value::Str(path) = &args[0] {
                    match std::fs::read_dir(path) {
                        Ok(entries) => {
                            let names: Vec<String> = entries
                                .filter_map(|e| e.ok())
                                .map(|e| e.file_name().to_string_lossy().into_owned())
                                .collect();
                            Ok(Value::Str(names.join("\n")))
                        }
                        Err(e) => Err(VMError::Str(format!(
                            "std::fs::read_dir(\"{}\"): {}",
                            path, e
                        ))),
                    }
                } else {
                    Err(VMError::Str(
                        "std::fs::read_dir: argument must be a string".into(),
                    ))
                }
            }
            "std::fs::metadata_size" => {
                if args.len() != 1 {
                    return Err(VMError::Str(format!(
                        "std::fs::metadata_size: expected 1 argument, got {}",
                        args.len()
                    )));
                }
                if let Value::Str(path) = &args[0] {
                    match std::fs::metadata(path) {
                        Ok(meta) => Ok(Value::Int(meta.len() as i64)),
                        Err(e) => Err(VMError::Str(format!(
                            "std::fs::metadata_size(\"{}\"): {}",
                            path, e
                        ))),
                    }
                } else {
                    Err(VMError::Str(
                        "std::fs::metadata_size: argument must be a string".into(),
                    ))
                }
            }
            "std::fs::copy" => {
                if args.len() != 2 {
                    return Err(VMError::Str(format!(
                        "std::fs::copy: expected 2 arguments (from, to), got {}",
                        args.len()
                    )));
                }
                if let (Value::Str(from), Value::Str(to)) = (&args[0], &args[1]) {
                    match std::fs::copy(from, to) {
                        Ok(bytes) => Ok(Value::Int(bytes as i64)),
                        Err(e) => Err(VMError::Str(format!(
                            "std::fs::copy(\"{}\", \"{}\"): {}",
                            from, to, e
                        ))),
                    }
                } else {
                    Err(VMError::Str(
                        "std::fs::copy: both arguments must be strings".into(),
                    ))
                }
            }
            "std::fs::rename" => {
                if args.len() != 2 {
                    return Err(VMError::Str(format!(
                        "std::fs::rename: expected 2 arguments (from, to), got {}",
                        args.len()
                    )));
                }
                if let (Value::Str(from), Value::Str(to)) = (&args[0], &args[1]) {
                    match std::fs::rename(from, to) {
                        Ok(_) => Ok(Value::Bool(true)),
                        Err(e) => Err(VMError::Str(format!(
                            "std::fs::rename(\"{}\", \"{}\"): {}",
                            from, to, e
                        ))),
                    }
                } else {
                    Err(VMError::Str(
                        "std::fs::rename: both arguments must be strings".into(),
                    ))
                }
            }
            "std::env::var" => {
                if args.len() != 1 {
                    return Err(VMError::Str(format!(
                        "std::env::var: expected 1 argument, got {}",
                        args.len()
                    )));
                }
                if let Value::Str(key) = &args[0] {
                    match std::env::var(key) {
                        Ok(val) => Ok(Value::Str(val)),
                        Err(_) => Ok(Value::None),
                    }
                } else {
                    Err(VMError::Str("std::env::var: argument must be a string".into()))
                }
            }
            "std::env::set_var" => {
                if args.len() != 2 {
                    return Err(VMError::Str(format!(
                        "std::env::set_var: expected 2 arguments, got {}",
                        args.len()
                    )));
                }
                if let (Value::Str(key), Value::Str(val)) = (&args[0], &args[1]) {
                    std::env::set_var(key, val);
                    Ok(Value::Bool(true))
                } else {
                    Err(VMError::Str(
                        "std::env::set_var: both arguments must be strings".into(),
                    ))
                }
            }
            "std::env::remove_var" => {
                if args.len() != 1 {
                    return Err(VMError::Str(format!(
                        "std::env::remove_var: expected 1 argument, got {}",
                        args.len()
                    )));
                }
                if let Value::Str(key) = &args[0] {
                    std::env::remove_var(key);
                    Ok(Value::Bool(true))
                } else {
                    Err(VMError::Str(
                        "std::env::remove_var: argument must be a string".into(),
                    ))
                }
            }
            "std::env::current_dir" => match std::env::current_dir() {
                Ok(path) => Ok(Value::Str(path.to_string_lossy().into_owned())),
                Err(e) => Err(VMError::Str(format!("std::env::current_dir: {}", e))),
            },
            "std::env::set_current_dir" => {
                if args.len() != 1 {
                    return Err(VMError::Str(format!(
                        "std::env::set_current_dir: expected 1 argument, got {}",
                        args.len()
                    )));
                }
                if let Value::Str(path) = &args[0] {
                    match std::env::set_current_dir(path) {
                        Ok(_) => Ok(Value::Bool(true)),
                        Err(e) => Err(VMError::Str(format!(
                            "std::env::set_current_dir(\"{}\"): {}",
                            path, e
                        ))),
                    }
                } else {
                    Err(VMError::Str(
                        "std::env::set_current_dir: argument must be a string".into(),
                    ))
                }
            }
            "std::env::args" => {
                let args_vec: Vec<String> = std::env::args().collect();
                Ok(Value::Str(args_vec.join("\n")))
            }
            "std::env::temp_dir" => {
                Ok(Value::Str(std::env::temp_dir().to_string_lossy().into_owned()))
            }
            "std::env::home_dir" => match std::env::var("HOME") {
                Ok(h) => Ok(Value::Str(h)),
                Err(_) => match std::env::var("USERPROFILE") {
                    Ok(h) => Ok(Value::Str(h)),
                    Err(_) => Ok(Value::None),
                },
            },
            "std::path::join" => {
                if args.len() < 2 {
                    return Err(VMError::Str(format!(
                        "std::path::join: expected 2+ arguments, got {}",
                        args.len()
                    )));
                }
                let mut path = std::path::PathBuf::new();
                for arg in &args {
                    if let Value::Str(s) = arg {
                        path.push(s);
                    }
                }
                Ok(Value::Str(path.to_string_lossy().into_owned()))
            }
            "std::path::file_name" => {
                if args.len() != 1 {
                    return Err(VMError::Str(format!(
                        "std::path::file_name: expected 1 argument, got {}",
                        args.len()
                    )));
                }
                if let Value::Str(p) = &args[0] {
                    match std::path::Path::new(p).file_name() {
                        Some(name) => Ok(Value::Str(name.to_string_lossy().into_owned())),
                        None => Ok(Value::None),
                    }
                } else {
                    Err(VMError::Str(
                        "std::path::file_name: argument must be a string".into(),
                    ))
                }
            }
            "std::path::file_stem" => {
                if args.len() != 1 {
                    return Err(VMError::Str(format!(
                        "std::path::file_stem: expected 1 argument, got {}",
                        args.len()
                    )));
                }
                if let Value::Str(p) = &args[0] {
                    match std::path::Path::new(p).file_stem() {
                        Some(stem) => Ok(Value::Str(stem.to_string_lossy().into_owned())),
                        None => Ok(Value::None),
                    }
                } else {
                    Err(VMError::Str(
                        "std::path::file_stem: argument must be a string".into(),
                    ))
                }
            }
            "std::path::extension" => {
                if args.len() != 1 {
                    return Err(VMError::Str(format!(
                        "std::path::extension: expected 1 argument, got {}",
                        args.len()
                    )));
                }
                if let Value::Str(p) = &args[0] {
                    match std::path::Path::new(p).extension() {
                        Some(ext) => Ok(Value::Str(ext.to_string_lossy().into_owned())),
                        None => Ok(Value::None),
                    }
                } else {
                    Err(VMError::Str(
                        "std::path::extension: argument must be a string".into(),
                    ))
                }
            }
            "std::path::parent" => {
                if args.len() != 1 {
                    return Err(VMError::Str(format!(
                        "std::path::parent: expected 1 argument, got {}",
                        args.len()
                    )));
                }
                if let Value::Str(p) = &args[0] {
                    match std::path::Path::new(p).parent() {
                        Some(parent) => {
                            Ok(Value::Str(parent.to_string_lossy().into_owned()))
                        }
                        None => Ok(Value::None),
                    }
                } else {
                    Err(VMError::Str(
                        "std::path::parent: argument must be a string".into(),
                    ))
                }
            }
            "std::path::is_absolute" => {
                if args.len() != 1 {
                    return Err(VMError::Str(format!(
                        "std::path::is_absolute: expected 1 argument, got {}",
                        args.len()
                    )));
                }
                if let Value::Str(p) = &args[0] {
                    Ok(Value::Bool(std::path::Path::new(p).is_absolute()))
                } else {
                    Err(VMError::Str(
                        "std::path::is_absolute: argument must be a string".into(),
                    ))
                }
            }
            "std::process::command" => {
                if args.is_empty() {
                    return Err(VMError::Str(
                        "std::process::command: expected at least 1 argument (command string)"
                            .into(),
                    ));
                }
                if let Value::Str(cmd) = &args[0] {
                    let mut command = if cfg!(target_os = "windows") {
                        let mut c = std::process::Command::new("cmd");
                        c.args(["/C", cmd]);
                        c
                    } else {
                        let mut c = std::process::Command::new("sh");
                        c.args(["-c", cmd]);
                        c
                    };
                    match command.output() {
                        Ok(output) => {
                            let stdout = String::from_utf8_lossy(&output.stdout);
                            Ok(Value::Str(stdout.trim_end().to_string()))
                        }
                        Err(e) => Err(VMError::Str(format!(
                            "std::process::command(\"{}\"): failed to execute: {}",
                            cmd, e
                        ))),
                    }
                } else {
                    Err(VMError::Str(
                        "std::process::command: argument must be a string".into(),
                    ))
                }
            }
            "std::process::command_status" => {
                if args.is_empty() {
                    return Err(VMError::Str(
                        "std::process::command_status: expected 1 argument".into(),
                    ));
                }
                if let Value::Str(cmd) = &args[0] {
                    let status = if cfg!(target_os = "windows") {
                        std::process::Command::new("cmd").args(["/C", cmd]).status()
                    } else {
                        std::process::Command::new("sh").args(["-c", cmd]).status()
                    };
                    match status {
                        Ok(s) => Ok(Value::Int(s.code().unwrap_or(-1) as i64)),
                        Err(e) => Err(VMError::Str(format!(
                            "std::process::command_status(\"{}\"): {}",
                            cmd, e
                        ))),
                    }
                } else {
                    Err(VMError::Str(
                        "std::process::command_status: argument must be a string".into(),
                    ))
                }
            }
            "std::process::exit" => {
                let code = if args.len() == 1 {
                    match &args[0] {
                        Value::Int(i) => *i as i32,
                        _ => 0,
                    }
                } else {
                    0
                };
                std::process::exit(code);
            }
            "std::process::id" => Ok(Value::Int(std::process::id() as i64)),
            "std::time::now_secs" => {
                let secs = std::time::SystemTime::now()
                    .duration_since(std::time::UNIX_EPOCH)
                    .unwrap()
                    .as_secs();
                Ok(Value::Int(secs as i64))
            }
            "std::time::now_millis" => {
                let millis = std::time::SystemTime::now()
                    .duration_since(std::time::UNIX_EPOCH)
                    .unwrap()
                    .as_millis();
                Ok(Value::Int(millis as i64))
            }
            "std::time::now_nanos" => {
                let nanos = std::time::SystemTime::now()
                    .duration_since(std::time::UNIX_EPOCH)
                    .unwrap()
                    .as_nanos();
                Ok(Value::Int(nanos as i64))
            }
            "std::time::sleep_ms" => {
                if args.len() != 1 {
                    return Err(VMError::Str(format!(
                        "std::time::sleep_ms: expected 1 argument, got {}",
                        args.len()
                    )));
                }
                let ms = match &args[0] {
                    Value::Int(i) => *i as u64,
                    Value::Float(f) => *f as u64,
                    _ => {
                        return Err(VMError::Str(
                            "std::time::sleep_ms: argument must be a number".into(),
                        ))
                    }
                };
                std::thread::sleep(std::time::Duration::from_millis(ms));
                Ok(Value::None)
            }
            "std::time::sleep_secs" => {
                if args.len() != 1 {
                    return Err(VMError::Str(format!(
                        "std::time::sleep_secs: expected 1 argument, got {}",
                        args.len()
                    )));
                }
                let secs = match &args[0] {
                    Value::Int(i) => *i as u64,
                    Value::Float(f) => *f as u64,
                    _ => {
                        return Err(VMError::Str(
                            "std::time::sleep_secs: argument must be a number".into(),
                        ))
                    }
                };
                std::thread::sleep(std::time::Duration::from_secs(secs));
                Ok(Value::None)
            }
            "std::thread::sleep_ms" => {
                if args.len() != 1 {
                    return Err(VMError::Str(format!(
                        "std::thread::sleep_ms: expected 1 argument, got {}",
                        args.len()
                    )));
                }
                let ms = match &args[0] {
                    Value::Int(i) => *i as u64,
                    Value::Float(f) => *f as u64,
                    _ => {
                        return Err(VMError::Str(
                            "std::thread::sleep_ms: argument must be a number".into(),
                        ))
                    }
                };
                std::thread::sleep(std::time::Duration::from_millis(ms));
                Ok(Value::None)
            }
            "std::thread::available_parallelism" => match std::thread::available_parallelism()
            {
                Ok(n) => Ok(Value::Int(n.get() as i64)),
                Err(_) => Ok(Value::Int(1)),
            },
            "std::collections::hash_string" => {
                if args.len() != 1 {
                    return Err(VMError::Str(format!(
                        "std::collections::hash_string: expected 1 argument, got {}",
                        args.len()
                    )));
                }
                if let Value::Str(s) = &args[0] {
                    use std::collections::hash_map::DefaultHasher;
                    use std::hash::{Hash, Hasher};
                    let mut hasher = DefaultHasher::new();
                    s.hash(&mut hasher);
                    Ok(Value::Int(hasher.finish() as i64))
                } else {
                    Err(VMError::Str(
                        "std::collections::hash_string: argument must be a string".into(),
                    ))
                }
            }
            "std::collections::hash_int" => {
                if args.len() != 1 {
                    return Err(VMError::Str(format!(
                        "std::collections::hash_int: expected 1 argument, got {}",
                        args.len()
                    )));
                }
                if let Value::Int(i) = &args[0] {
                    use std::collections::hash_map::DefaultHasher;
                    use std::hash::{Hash, Hasher};
                    let mut hasher = DefaultHasher::new();
                    i.hash(&mut hasher);
                    Ok(Value::Int(hasher.finish() as i64))
                } else {
                    Err(VMError::Str(
                        "std::collections::hash_int: argument must be an integer".into(),
                    ))
                }
            }
            "std::convert::parse_int" => {
                if args.len() != 1 {
                    return Err(VMError::Str(format!(
                        "std::convert::parse_int: expected 1 argument, got {}",
                        args.len()
                    )));
                }
                if let Value::Str(s) = &args[0] {
                    match s.trim().parse::<i64>() {
                        Ok(i) => Ok(Value::Int(i)),
                        Err(_) => Ok(Value::None),
                    }
                } else {
                    Err(VMError::Str(
                        "std::convert::parse_int: argument must be a string".into(),
                    ))
                }
            }
            "std::convert::parse_float" => {
                if args.len() != 1 {
                    return Err(VMError::Str(format!(
                        "std::convert::parse_float: expected 1 argument, got {}",
                        args.len()
                    )));
                }
                if let Value::Str(s) = &args[0] {
                    match s.trim().parse::<f64>() {
                        Ok(f) => Ok(Value::Float(f)),
                        Err(_) => Ok(Value::None),
                    }
                } else {
                    Err(VMError::Str(
                        "std::convert::parse_float: argument must be a string".into(),
                    ))
                }
            }
            "std::convert::to_string" => {
                if args.len() != 1 {
                    return Err(VMError::Str(format!(
                        "std::convert::to_string: expected 1 argument, got {}",
                        args.len()
                    )));
                }
                Ok(Value::Str(value_to_string(&args[0])))
            }
            "std::convert::int_to_char" => {
                if args.len() != 1 {
                    return Err(VMError::Str(format!(
                        "std::convert::int_to_char: expected 1 argument, got {}",
                        args.len()
                    )));
                }
                if let Value::Int(i) = &args[0] {
                    match char::from_u32(*i as u32) {
                        Some(c) => Ok(Value::Str(c.to_string())),
                        None => Ok(Value::None),
                    }
                } else {
                    Err(VMError::Str(
                        "std::convert::int_to_char: argument must be an integer".into(),
                    ))
                }
            }
            "std::convert::char_to_int" => {
                if args.len() != 1 {
                    return Err(VMError::Str(format!(
                        "std::convert::char_to_int: expected 1 argument, got {}",
                        args.len()
                    )));
                }
                if let Value::Str(s) = &args[0] {
                    match s.chars().next() {
                        Some(c) => Ok(Value::Int(c as i64)),
                        None => Ok(Value::None),
                    }
                } else {
                    Err(VMError::Str(
                        "std::convert::char_to_int: argument must be a string".into(),
                    ))
                }
            }
            "std::io::read_line" => {
                let mut input = String::new();
                io::stdin().read_line(&mut input).unwrap();
                Ok(Value::Str(
                    input
                        .trim_end_matches('\n')
                        .trim_end_matches('\r')
                        .to_string(),
                ))
            }
            "std::io::write" => {
                if args.len() != 1 {
                    return Err(VMError::Str(format!(
                        "std::io::write: expected 1 argument, got {}",
                        args.len()
                    )));
                }
                let s = value_to_string(&args[0]);
                print!("{}", s);
                io::stdout().flush().unwrap();
                Ok(Value::None)
            }
            "std::io::writeln" => {
                if args.len() != 1 {
                    return Err(VMError::Str(format!(
                        "std::io::writeln: expected 1 argument, got {}",
                        args.len()
                    )));
                }
                let s = value_to_string(&args[0]);
                println!("{}", s);
                Ok(Value::None)
            }
            "std::io::eprint" => {
                if args.len() != 1 {
                    return Err(VMError::Str(format!(
                        "std::io::eprint: expected 1 argument, got {}",
                        args.len()
                    )));
                }
                let s = value_to_string(&args[0]);
                eprint!("{}", s);
                Ok(Value::None)
            }
            "std::io::eprintln" => {
                if args.len() != 1 {
                    return Err(VMError::Str(format!(
                        "std::io::eprintln: expected 1 argument, got {}",
                        args.len()
                    )));
                }
                let s = value_to_string(&args[0]);
                eprintln!("{}", s);
                Ok(Value::None)
            }
            "std::string::repeat" => {
                if args.len() != 2 {
                    return Err(VMError::Str(format!(
                        "std::string::repeat: expected 2 arguments (str, count), got {}",
                        args.len()
                    )));
                }
                if let (Value::Str(s), Value::Int(n)) = (&args[0], &args[1]) {
                    if *n < 0 {
                        return Err(VMError::Str(
                            "std::string::repeat: count must be non-negative".into(),
                        ));
                    }
                    Ok(Value::Str(s.repeat(*n as usize)))
                } else {
                    Err(VMError::Str(
                        "std::string::repeat: arguments must be (string, int)".into(),
                    ))
                }
            }
            "std::string::contains" => {
                if args.len() != 2 {
                    return Err(VMError::Str(format!(
                        "std::string::contains: expected 2 arguments, got {}",
                        args.len()
                    )));
                }
                if let (Value::Str(haystack), Value::Str(needle)) = (&args[0], &args[1]) {
                    Ok(Value::Bool(haystack.contains(needle.as_str())))
                } else {
                    Err(VMError::Str(
                        "std::string::contains: arguments must be (string, string)".into(),
                    ))
                }
            }
            "std::string::replace" => {
                if args.len() != 3 {
                    return Err(VMError::Str(format!(
                        "std::string::replace: expected 3 arguments (str, from, to), got {}",
                        args.len()
                    )));
                }
                if let (Value::Str(s), Value::Str(from), Value::Str(to)) =
                    (&args[0], &args[1], &args[2])
                {
                    Ok(Value::Str(s.replace(from.as_str(), to.as_str())))
                } else {
                    Err(VMError::Str(
                        "std::string::replace: arguments must be (string, string, string)"
                            .into(),
                    ))
                }
            }
            "std::string::split" => {
                if args.len() != 2 {
                    return Err(VMError::Str(format!(
                        "std::string::split: expected 2 arguments (str, delimiter), got {}",
                        args.len()
                    )));
                }
                if let (Value::Str(s), Value::Str(delim)) = (&args[0], &args[1]) {
                    let parts: Vec<&str> = s.split(delim.as_str()).collect();
                    Ok(Value::Str(parts.join("\n")))
                } else {
                    Err(VMError::Str(
                        "std::string::split: arguments must be (string, string)".into(),
                    ))
                }
            }
            "std::string::trim" => {
                if args.len() != 1 {
                    return Err(VMError::Str(format!(
                        "std::string::trim: expected 1 argument, got {}",
                        args.len()
                    )));
                }
                if let Value::Str(s) = &args[0] {
                    Ok(Value::Str(s.trim().to_string()))
                } else {
                    Err(VMError::Str(
                        "std::string::trim: argument must be a string".into(),
                    ))
                }
            }
            "std::string::to_uppercase" => {
                if args.len() != 1 {
                    return Err(VMError::Str(format!(
                        "std::string::to_uppercase: expected 1 argument, got {}",
                        args.len()
                    )));
                }
                if let Value::Str(s) = &args[0] {
                    Ok(Value::Str(s.to_uppercase()))
                } else {
                    Err(VMError::Str(
                        "std::string::to_uppercase: argument must be a string".into(),
                    ))
                }
            }
            "std::string::to_lowercase" => {
                if args.len() != 1 {
                    return Err(VMError::Str(format!(
                        "std::string::to_lowercase: expected 1 argument, got {}",
                        args.len()
                    )));
                }
                if let Value::Str(s) = &args[0] {
                    Ok(Value::Str(s.to_lowercase()))
                } else {
                    Err(VMError::Str(
                        "std::string::to_lowercase: argument must be a string".into(),
                    ))
                }
            }
            "std::string::len" => {
                if args.len() != 1 {
                    return Err(VMError::Str(format!(
                        "std::string::len: expected 1 argument, got {}",
                        args.len()
                    )));
                }
                if let Value::Str(s) = &args[0] {
                    Ok(Value::Int(s.len() as i64))
                } else {
                    Err(VMError::Str("std::string::len: argument must be a string".into()))
                }
            }
            "std::string::chars_count" => {
                if args.len() != 1 {
                    return Err(VMError::Str(format!(
                        "std::string::chars_count: expected 1 argument, got {}",
                        args.len()
                    )));
                }
                if let Value::Str(s) = &args[0] {
                    Ok(Value::Int(s.chars().count() as i64))
                } else {
                    Err(VMError::Str(
                        "std::string::chars_count: argument must be a string".into(),
                    ))
                }
            }
            "std::string::starts_with" => {
                if args.len() != 2 {
                    return Err(VMError::Str(format!(
                        "std::string::starts_with: expected 2 arguments, got {}",
                        args.len()
                    )));
                }
                if let (Value::Str(s), Value::Str(prefix)) = (&args[0], &args[1]) {
                    Ok(Value::Bool(s.starts_with(prefix.as_str())))
                } else {
                    Err(VMError::Str(
                        "std::string::starts_with: arguments must be (string, string)".into(),
                    ))
                }
            }
            "std::string::ends_with" => {
                if args.len() != 2 {
                    return Err(VMError::Str(format!(
                        "std::string::ends_with: expected 2 arguments, got {}",
                        args.len()
                    )));
                }
                if let (Value::Str(s), Value::Str(suffix)) = (&args[0], &args[1]) {
                    Ok(Value::Bool(s.ends_with(suffix.as_str())))
                } else {
                    Err(VMError::Str(
                        "std::string::ends_with: arguments must be (string, string)".into(),
                    ))
                }
            }
            "std::string::substring" => {
                if args.len() != 3 {
                    return Err(VMError::Str(format!(
                        "std::string::substring: expected 3 arguments (str, start, end), got {}",
                        args.len()
                    )));
                }
                if let (Value::Str(s), Value::Int(start), Value::Int(end)) =
                    (&args[0], &args[1], &args[2])
                {
                    let chars: Vec<char> = s.chars().collect();
                    let start = (*start).max(0) as usize;
                    let end = (*end).min(chars.len() as i64) as usize;
                    if start >= end || start >= chars.len() {
                        Ok(Value::Str(String::new()))
                    } else {
                        Ok(Value::Str(chars[start..end].iter().collect()))
                    }
                } else {
                    Err(VMError::Str(
                        "std::string::substring: arguments must be (string, int, int)".into(),
                    ))
                }
            }
            "std::string::find" => {
                if args.len() != 2 {
                    return Err(VMError::Str(format!(
                        "std::string::find: expected 2 arguments, got {}",
                        args.len()
                    )));
                }
                if let (Value::Str(s), Value::Str(needle)) = (&args[0], &args[1]) {
                    match s.find(needle.as_str()) {
                        Some(pos) => Ok(Value::Int(pos as i64)),
                        None => Ok(Value::Int(-1)),
                    }
                } else {
                    Err(VMError::Str(
                        "std::string::find: arguments must be (string, string)".into(),
                    ))
                }
            }
            "std::math::abs" => {
                if args.len() != 1 {
                    return Err(VMError::Str(format!(
                        "std::math::abs: expected 1 argument, got {}",
                        args.len()
                    )));
                }
                match &args[0] {
                    Value::Int(i) => Ok(Value::Int(i.abs())),
                    Value::Float(f) => Ok(Value::Float(f.abs())),
                    other => Err(VMError::Str(format!(
                        "std::math::abs: argument must be a number, got {}",
                        other.type_name()
                    ))),
                }
            }
            "std::math::pow" => {
                if args.len() != 2 {
                    return Err(VMError::Str(format!(
                        "std::math::pow: expected 2 arguments (base, exp), got {}",
                        args.len()
                    )));
                }
                match (&args[0], &args[1]) {
                    (Value::Int(base), Value::Int(exp)) => {
                        if *exp >= 0 {
                            Ok(Value::Int(base.pow(*exp as u32)))
                        } else {
                            Ok(Value::Float((*base as f64).powi(*exp as i32)))
                        }
                    }
                    (Value::Float(base), Value::Float(exp)) => {
                        Ok(Value::Float(base.powf(*exp)))
                    }
                    (Value::Int(base), Value::Float(exp)) => {
                        Ok(Value::Float((*base as f64).powf(*exp)))
                    }
                    (Value::Float(base), Value::Int(exp)) => {
                        Ok(Value::Float(base.powi(*exp as i32)))
                    }
                    _ => Err(VMError::Str(
                        "std::math::pow: both arguments must be numbers".into(),
                    )),
                }
            }
            "std::math::sqrt" => {
                if args.len() != 1 {
                    return Err(VMError::Str(format!(
                        "std::math::sqrt: expected 1 argument, got {}",
                        args.len()
                    )));
                }
                match &args[0] {
                    Value::Int(i) => {
                        if *i < 0 {
                            return Err(VMError::Str(format!(
                                "std::math::sqrt: cannot take square root of negative number {}",
                                i
                            )));
                        }
                        Ok(Value::Float((*i as f64).sqrt()))
                    }
                    Value::Float(f) => {
                        if *f < 0.0 {
                            return Err(VMError::Str(format!(
                                "std::math::sqrt: cannot take square root of negative number {}",
                                f
                            )));
                        }
                        Ok(Value::Float(f.sqrt()))
                    }
                    other => Err(VMError::Str(format!(
                        "std::math::sqrt: argument must be a number, got {}",
                        other.type_name()
                    ))),
                }
            }
            "std::math::sin" => {
                if args.len() != 1 {
                    return Err(VMError::Str(format!(
                        "std::math::sin: expected 1 argument, got {}",
                        args.len()
                    )));
                }
                match &args[0] {
                    Value::Int(i) => Ok(Value::Float((*i as f64).sin())),
                    Value::Float(f) => Ok(Value::Float(f.sin())),
                    other => Err(VMError::Str(format!(
                        "std::math::sin: argument must be a number, got {}",
                        other.type_name()
                    ))),
                }
            }
            "std::math::cos" => {
                if args.len() != 1 {
                    return Err(VMError::Str(format!(
                        "std::math::cos: expected 1 argument, got {}",
                        args.len()
                    )));
                }
                match &args[0] {
                    Value::Int(i) => Ok(Value::Float((*i as f64).cos())),
                    Value::Float(f) => Ok(Value::Float(f.cos())),
                    other => Err(VMError::Str(format!(
                        "std::math::cos: argument must be a number, got {}",
                        other.type_name()
                    ))),
                }
            }
            "std::math::tan" => {
                if args.len() != 1 {
                    return Err(VMError::Str(format!(
                        "std::math::tan: expected 1 argument, got {}",
                        args.len()
                    )));
                }
                match &args[0] {
                    Value::Int(i) => Ok(Value::Float((*i as f64).tan())),
                    Value::Float(f) => Ok(Value::Float(f.tan())),
                    other => Err(VMError::Str(format!(
                        "std::math::tan: argument must be a number, got {}",
                        other.type_name()
                    ))),
                }
            }
            "std::math::log" => {
                if args.len() != 1 {
                    return Err(VMError::Str(format!(
                        "std::math::log: expected 1 argument, got {}",
                        args.len()
                    )));
                }
                match &args[0] {
                    Value::Int(i) => Ok(Value::Float((*i as f64).ln())),
                    Value::Float(f) => Ok(Value::Float(f.ln())),
                    other => Err(VMError::Str(format!(
                        "std::math::log: argument must be a number, got {}",
                        other.type_name()
                    ))),
                }
            }
            "std::math::log2" => {
                if args.len() != 1 {
                    return Err(VMError::Str(format!(
                        "std::math::log2: expected 1 argument, got {}",
                        args.len()
                    )));
                }
                match &args[0] {
                    Value::Int(i) => Ok(Value::Float((*i as f64).log2())),
                    Value::Float(f) => Ok(Value::Float(f.log2())),
                    other => Err(VMError::Str(format!(
                        "std::math::log2: argument must be a number, got {}",
                        other.type_name()
                    ))),
                }
            }
            "std::math::log10" => {
                if args.len() != 1 {
                    return Err(VMError::Str(format!(
                        "std::math::log10: expected 1 argument, got {}",
                        args.len()
                    )));
                }
                match &args[0] {
                    Value::Int(i) => Ok(Value::Float((*i as f64).log10())),
                    Value::Float(f) => Ok(Value::Float(f.log10())),
                    other => Err(VMError::Str(format!(
                        "std::math::log10: argument must be a number, got {}",
                        other.type_name()
                    ))),
                }
            }
            "std::math::min" => {
                if args.len() != 2 {
                    return Err(VMError::Str(format!(
                        "std::math::min: expected 2 arguments, got {}",
                        args.len()
                    )));
                }
                match (&args[0], &args[1]) {
                    (Value::Int(a), Value::Int(b)) => Ok(Value::Int(*a.min(b))),
                    (Value::Float(a), Value::Float(b)) => Ok(Value::Float(a.min(*b))),
                    (Value::Int(a), Value::Float(b)) => Ok(Value::Float((*a as f64).min(*b))),
                    (Value::Float(a), Value::Int(b)) => Ok(Value::Float(a.min(*b as f64))),
                    _ => Err(VMError::Str(
                        "std::math::min: both arguments must be numbers".into(),
                    )),
                }
            }
            "std::math::max" => {
                if args.len() != 2 {
                    return Err(VMError::Str(format!(
                        "std::math::max: expected 2 arguments, got {}",
                        args.len()
                    )));
                }
                match (&args[0], &args[1]) {
                    (Value::Int(a), Value::Int(b)) => Ok(Value::Int(*a.max(b))),
                    (Value::Float(a), Value::Float(b)) => Ok(Value::Float(a.max(*b))),
                    (Value::Int(a), Value::Float(b)) => Ok(Value::Float((*a as f64).max(*b))),
                    (Value::Float(a), Value::Int(b)) => Ok(Value::Float(a.max(*b as f64))),
                    _ => Err(VMError::Str(
                        "std::math::max: both arguments must be numbers".into(),
                    )),
                }
            }
            "std::math::floor" => {
                if args.len() != 1 {
                    return Err(VMError::Str(format!(
                        "std::math::floor: expected 1 argument, got {}",
                        args.len()
                    )));
                }
                match &args[0] {
                    Value::Int(i) => Ok(Value::Int(*i)),
                    Value::Float(f) => Ok(Value::Int(f.floor() as i64)),
                    other => Err(VMError::Str(format!(
                        "std::math::floor: argument must be a number, got {}",
                        other.type_name()
                    ))),
                }
            }
            "std::math::ceil" => {
                if args.len() != 1 {
                    return Err(VMError::Str(format!(
                        "std::math::ceil: expected 1 argument, got {}",
                        args.len()
                    )));
                }
                match &args[0] {
                    Value::Int(i) => Ok(Value::Int(*i)),
                    Value::Float(f) => Ok(Value::Int(f.ceil() as i64)),
                    other => Err(VMError::Str(format!(
                        "std::math::ceil: argument must be a number, got {}",
                        other.type_name()
                    ))),
                }
            }
            "std::math::round" => {
                if args.len() != 1 {
                    return Err(VMError::Str(format!(
                        "std::math::round: expected 1 argument, got {}",
                        args.len()
                    )));
                }
                match &args[0] {
                    Value::Int(i) => Ok(Value::Int(*i)),
                    Value::Float(f) => Ok(Value::Int(f.round() as i64)),
                    other => Err(VMError::Str(format!(
                        "std::math::round: argument must be a number, got {}",
                        other.type_name()
                    ))),
                }
            }
            "std::math::pi" => Ok(Value::Float(std::f64::consts::PI)),
            "std::math::e" => Ok(Value::Float(std::f64::consts::E)),
            "std::math::tau" => Ok(Value::Float(std::f64::consts::TAU)),
            _ => {
                // Provide helpful suggestions
                let module = path.rsplit("::").nth(1).unwrap_or("");
                let available: Vec<&str> = match path.split("::").take(2).collect::<Vec<_>>().as_slice() {
                    ["std", "fs"] => vec![
                        "read_to_string", "write", "append", "remove_file", "remove_dir",
                        "exists", "is_file", "is_dir", "create_dir", "read_dir",
                        "metadata_size", "copy", "rename",
                    ],
                    ["std", "env"] => vec![
                        "var", "set_var", "remove_var", "current_dir",
                        "set_current_dir", "args", "temp_dir", "home_dir",
                    ],
                    ["std", "path"] => vec![
                        "join", "file_name", "file_stem", "extension", "parent", "is_absolute",
                    ],
                    ["std", "process"] => vec!["command", "command_status", "exit", "id"],
                    ["std", "time"] => vec![
                        "now_secs", "now_millis", "now_nanos", "sleep_ms", "sleep_secs",
                    ],
                    ["std", "thread"] => vec!["sleep_ms", "available_parallelism"],
                    ["std", "collections"] => vec!["hash_string", "hash_int"],
                    ["std", "convert"] => vec![
                        "parse_int", "parse_float", "to_string", "int_to_char", "char_to_int",
                    ],
                    ["std", "io"] => vec!["read_line", "write", "writeln", "eprint", "eprintln"],
                    ["std", "string"] => vec![
                        "repeat", "contains", "replace", "split", "trim",
                        "to_uppercase", "to_lowercase", "len", "chars_count",
                        "starts_with", "ends_with", "substring", "find",
                    ],
                    ["std", "math"] => vec![
                        "abs", "pow", "sqrt", "sin", "cos", "tan", "log", "log2", "log10",
                        "min", "max", "floor", "ceil", "round", "pi", "e", "tau",
                    ],
                    _ => vec![],
                };

                if !available.is_empty() {
                    let func_name = path.rsplit("::").next().unwrap_or("");
                    Err(VMError::Str(format!(
                        "Unknown function '{}' in module 'std::{}'.\n  Available functions: {}",
                        func_name,
                        module,
                        available.join(", ")
                    )))
                } else {
                    Err(VMError::Str(format!(
                        "Unknown Rust module function: '{}'.\n  Supported modules: std::fs, std::env, std::path, std::process, std::time, std::thread, std::collections, std::convert, std::io, std::string, std::math",
                        path
                    )))
                }
            }
        }
    }

    fn handle_rust_module_import(&mut self, module_path: &str) -> Result<(), VMError> {
        let valid_prefixes = [
            "std::fs",
            "std::env",
            "std::path",
            "std::process",
            "std::time",
            "std::thread",
            "std::collections",
            "std::convert",
            "std::io",
            "std::string",
            "std::math",
        ];

        let is_valid = valid_prefixes.iter().any(|prefix| {
            module_path == *prefix || module_path.starts_with(&format!("{}::", prefix))
        });

        if !is_valid {
            return Err(VMError::Str(format!(
                "Unknown Rust module: '{}'.\n  Supported modules: std::fs, std::env, std::path, std::process, std::time, std::thread, std::collections, std::convert, std::io, std::string, std::math",
                module_path
            )));
        }

        let var_name = module_path.replace("::", "_");
        self.vars
            .insert(var_name.clone(), Value::RustModule(module_path.to_string()));
        self.rust_modules.push(module_path.to_string());

        Ok(())
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
                        return Err(VMError::Str(format!(
                            "math.sqrt: expected 1 argument, got {}",
                            args.len()
                        )));
                    }
                    match &args[0] {
                        Value::Int(i) => Ok(Value::Float((*i as f64).sqrt())),
                        Value::Float(f) => Ok(Value::Float(f.sqrt())),
                        other => Err(VMError::Str(format!(
                            "math.sqrt: argument must be a number, got {}",
                            other.type_name()
                        ))),
                    }
                }
                "pi" => Ok(Value::Float(std::f64::consts::PI)),
                "e" => Ok(Value::Float(std::f64::consts::E)),
                "floor" => {
                    if args.len() != 1 {
                        return Err(VMError::Str(format!(
                            "math.floor: expected 1 argument, got {}",
                            args.len()
                        )));
                    }
                    match &args[0] {
                        Value::Int(i) => Ok(Value::Int(*i)),
                        Value::Float(f) => Ok(Value::Int(f.floor() as i64)),
                        other => Err(VMError::Str(format!(
                            "math.floor: argument must be a number, got {}",
                            other.type_name()
                        ))),
                    }
                }
                "ceil" => {
                    if args.len() != 1 {
                        return Err(VMError::Str(format!(
                            "math.ceil: expected 1 argument, got {}",
                            args.len()
                        )));
                    }
                    match &args[0] {
                        Value::Int(i) => Ok(Value::Int(*i)),
                        Value::Float(f) => Ok(Value::Int(f.ceil() as i64)),
                        other => Err(VMError::Str(format!(
                            "math.ceil: argument must be a number, got {}",
                            other.type_name()
                        ))),
                    }
                }
                _ => Err(VMError::Str(format!(
                    "Unknown method '{}' on module 'math'. Available: sqrt, pi, e, floor, ceil",
                    method
                ))),
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
                        return Err(VMError::Str(format!(
                            "os.system: expected 1 argument (command), got {}",
                            args.len()
                        )));
                    }
                    if let Value::Str(cmd) = &args[0] {
                        let status = if cfg!(target_os = "windows") {
                            std::process::Command::new("cmd").args(["/C", cmd]).status()
                        } else {
                            std::process::Command::new("sh").args(["-c", cmd]).status()
                        };
                        match status {
                            Ok(s) => Ok(Value::Int(s.code().unwrap_or(0) as i64)),
                            Err(e) => Err(VMError::Str(format!("os.system: failed: {}", e))),
                        }
                    } else {
                        Err(VMError::Str(
                            "os.system: argument must be a string command".into(),
                        ))
                    }
                }
                _ => Err(VMError::Str(format!(
                    "Unknown method '{}' on module 'os'. Available: name, getcwd, system",
                    method
                ))),
            },
            "random" => match method {
                "randint" => {
                    if args.len() != 2 {
                        return Err(VMError::Str(format!(
                            "random.randint: expected 2 arguments (min, max), got {}",
                            args.len()
                        )));
                    }
                    let a = match &args[0] {
                        Value::Int(i) => *i,
                        other => {
                            return Err(VMError::Str(format!(
                                "random.randint: first argument must be an integer, got {}",
                                other.type_name()
                            )))
                        }
                    };
                    let b = match &args[1] {
                        Value::Int(i) => *i,
                        other => {
                            return Err(VMError::Str(format!(
                                "random.randint: second argument must be an integer, got {}",
                                other.type_name()
                            )))
                        }
                    };
                    let range = (b - a).unsigned_abs() + 1;
                    let res = a + (next_random() % range) as i64;
                    Ok(Value::Int(res))
                }
                _ => Err(VMError::Str(format!(
                    "Unknown method '{}' on module 'random'. Available: randint",
                    method
                ))),
            },
            _ => Err(VMError::Str(format!(
                "Unknown module '{}'. Built-in modules: math, os, random.\n  Hint: Use % \"std::...\" for Rust standard library modules.",
                mod_name
            ))),
        }
    }

    fn eval_rust_module_method(
        &mut self,
        mod_path: &str,
        method: &str,
        args: Vec<Value>,
    ) -> Result<Value, VMError> {
        let full_path = format!("{}::{}", mod_path, method);
        self.eval_rust_module_call(&full_path, args)
    }

    fn eval_method_with_args(
        &mut self,
        obj: Value,
        method: &str,
        args: Vec<Value>,
    ) -> Result<Value, VMError> {
        match obj {
            Value::Str(s) => {
                if !args.is_empty() {
                    return Err(VMError::Str(format!(
                        "String method '.{}' does not take arguments (got {}).\n  Hint: Did you mean to use $variable.method() to call a module method?\n  String methods (.U, .L, .S, .T, .C) take no arguments.",
                        method,
                        args.len()
                    )));
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
                            Some(f) => f.to_uppercase().collect::<String>() + c.as_str(),
                        }))
                    }
                    _ => Err(VMError::Str(format!(
                        "Unknown string method '.{}'. Available string methods: .U (uppercase), .L (lowercase), .S (trim), .T (title case), .C (capitalize)",
                        method
                    ))),
                }
            }
            Value::Module(mod_name) => self.eval_module_method(&mod_name, method, args),
            Value::RustModule(mod_path) => {
                self.eval_rust_module_method(&mod_path, method, args)
            }
            other => Err(VMError::Str(format!(
                "Cannot call method '.{}' on {} value ({}).\n  Methods are available on: strings, modules (via U), and Rust modules (via %).",
                method,
                other.type_name(),
                value_to_string(&other),
            ))),
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
            Expr::Str(s) => {
                if s.starts_with("%rust_mod%") {
                    let mod_path = &s["%rust_mod%".len()..];
                    return Ok(Value::RustModule(mod_path.to_string()));
                }
                Ok(Value::Str(s.clone()))
            }
            Expr::Var(v) => Ok(self.vars.get(v).cloned().unwrap_or(Value::None)),
            Expr::BoolLit(b) => Ok(Value::Bool(*b)),
            Expr::NoneLit => Ok(Value::None),
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
                        other => Err(VMError::Str(format!(
                            "Cannot negate {} value ({}). Unary '-' requires a number.",
                            other.type_name(),
                            value_to_string(&other)
                        ))),
                    },
                    "!" => Ok(Value::Bool(!r.as_bool())),
                    _ => Err(VMError::Str(format!("Unknown unary operator '{}'", op))),
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
            Expr::RustModuleCall(path, args) => {
                let mut eval_args = Vec::new();
                for a in args {
                    eval_args.push(self.eval_expr(a)?);
                }
                self.eval_rust_module_call(path, eval_args)
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
                Value::RustModule(m) => format!("\"{}\"", m),
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
            .map_err(|e| VMError::Str(format!("Python exec: failed to write temp script: {}", e)))?;

        let python_cmd = if cfg!(target_os = "windows") {
            "python"
        } else {
            "python3"
        };

        let output = std::process::Command::new(python_cmd)
            .arg(&script_path)
            .output()
            .map_err(|e| {
                VMError::Str(format!(
                    "Python exec: failed to run '{}' — is Python installed? ({})",
                    python_cmd, e
                ))
            })?;

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
                "Python exec: exited with code {}",
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
        let exe_path = temp_dir.join(if cfg!(target_os = "windows") {
            "vulpin_rust_exec.exe"
        } else {
            "vulpin_rust_exec"
        });

        let final_code = if code.contains("fn main") {
            code.to_string()
        } else {
            format!("fn main() {{\n{}\n}}", code)
        };

        std::fs::write(&script_path, final_code)
            .map_err(|e| VMError::Str(format!("Rust exec: failed to write temp file: {}", e)))?;

        let status = std::process::Command::new("rustc")
            .arg(&script_path)
            .arg("-o")
            .arg(&exe_path)
            .status()
            .map_err(|e| {
                VMError::Str(format!(
                    "Rust exec: failed to run 'rustc' — is Rust installed? ({})",
                    e
                ))
            })?;

        if !status.success() {
            return Err(VMError::Str(
                "Rust exec: compilation failed. Check your Rust syntax.".into(),
            ));
        }

        let output = std::process::Command::new(&exe_path)
            .output()
            .map_err(|e| VMError::Str(format!("Rust exec: failed to run binary: {}", e)))?;

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
                if !self.vars.contains_key(&var) {
                    eprintln!(
                        "{}",
                        format_warning(
                            self.ip,
                            &self.lines.get(self.ip.saturating_sub(1)).cloned().unwrap_or_default(),
                            &format!(
                                "Variable '{}' does not exist yet. Initializing to 0 before applying '{}'.",
                                var, op
                            )
                        )
                    );
                }
                let current = self.vars.get(&var).cloned().unwrap_or(Value::Int(0));
                let right = self.eval_expr(&expr)?;
                let new_val = self.eval_binop(&current, &op, &right)?;
                self.vars.insert(var, new_val);
            }
            Statement::StrReplace(var, old_expr, new_expr) => {
                if !self.vars.contains_key(&var) {
                    return Err(VMError::Str(format!(
                        "S (string replace): variable '{}' does not exist. Define it first (e.g., {} = \"some text\").",
                        var, var
                    )));
                }
                let s = self.vars.get(&var).cloned().unwrap_or(Value::Str("".into()));
                let old = self.eval_expr(&old_expr)?;
                let new = self.eval_expr(&new_expr)?;
                match (s, old, new) {
                    (Value::Str(mut s), Value::Str(old), Value::Str(new)) => {
                        s = s.replace(&old, &new);
                        self.vars.insert(var, Value::Str(s));
                    }
                    (other, _, _) => {
                        return Err(VMError::Str(format!(
                            "S (string replace): variable '{}' is {}, not a string. S only works on string variables.",
                            var,
                            other.type_name()
                        )));
                    }
                }
            }
            Statement::Delay(expr) => {
                let val = self.eval_expr(&expr)?;
                let ms = match val {
                    Value::Int(i) => (i as f64 * 1000.0) as u64,
                    Value::Float(f) => (f * 1000.0) as u64,
                    other => {
                        return Err(VMError::Str(format!(
                            "D (delay): expected a number (seconds), got {} ({})",
                            other.type_name(),
                            value_to_string(&other)
                        )))
                    }
                };
                std::thread::sleep(std::time::Duration::from_millis(ms));
            }
            Statement::Delete(var) => {
                if !self.vars.contains_key(&var) {
                    return Err(VMError::Str(format!(
                        "D (delete): variable '{}' does not exist. Cannot delete undefined variable.",
                        var
                    )));
                }
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
                    // Check if file exists BEFORE trying to read
                    if !std::path::Path::new(&file).exists() {
                        return Err(VMError::Str(format!(
                            "U (import): file '{}' not found.\n  Hint: Check the file path. Current directory: {}",
                            file,
                            std::env::current_dir()
                                .map(|p| p.to_string_lossy().into_owned())
                                .unwrap_or_else(|_| "unknown".into())
                        )));
                    }
                    match std::fs::read_to_string(&file) {
                        Ok(content) => {
                            let lines: Vec<String> =
                                content.lines().map(|s| s.to_string()).collect();
                            let mut sub_vm = VM::new(lines);
                            sub_vm.precompute();
                            if let Err(e) = sub_vm.run() {
                                return Err(VMError::Str(format!(
                                    "U (import): error in imported file '{}': {}",
                                    file, e
                                )));
                            }
                            self.vars.extend(sub_vm.vars);
                        }
                        Err(e) => {
                            return Err(VMError::Str(format!(
                                "U (import): failed to read file '{}': {}",
                                file, e
                            )));
                        }
                    }
                } else {
                    // Validate built-in module names
                    let known_modules = ["math", "os", "random"];
                    if !known_modules.contains(&file.as_str()) {
                        return Err(VMError::Str(format!(
                            "U (import): unknown module '{}'.\n  Built-in modules: math, os, random\n  For Rust std modules, use: % \"std::fs\", % \"std::env\", etc.\n  For .vul files, use: U \"filename.vul\"",
                            file
                        )));
                    }
                    self.vars.insert(file.clone(), Value::Module(file));
                }
            }
            Statement::RustModuleImport(module_path) => {
                self.handle_rust_module_import(&module_path)?;
            }
            Statement::RustModuleCall(expr) => {
                let _ = self.eval_expr(&expr)?;
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
                    let target = self.labels.get(&label).cloned().ok_or_else(|| {
                        VMError::Str(format!(
                            "Conditional jump: undefined label '{}'. Define it with: L {}",
                            label, label
                        ))
                    })?;
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
                        let current = self.vars.get(&var).cloned().unwrap_or(Value::Int(0));
                        let next = self.eval_binop(&current, "+", &step)?;
                        self.vars.insert(var.clone(), next.clone());
                        if self.in_range(&next, &end, &step) {
                            self.loop_stack
                                .push(LoopFrame::For(start_ip, var, end, step));
                            self.ip = start_ip + 1;
                        } else {
                            let end_ip = self.block_info[start_ip].matching_end.unwrap();
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
                let target = self.labels.get(&label).cloned().ok_or_else(|| {
                    VMError::Str(format!(
                        "J (jump): undefined label '{}'. Define it with: L {}",
                        label, label
                    ))
                })?;
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
                while self.ip < self.lines.len() {
                    let next_line = &self.lines[self.ip];
                    let stripped = strip_comment(next_line);
                    let trimmed = stripped.trim();
                    if trimmed.is_empty() {
                        let mut has_more = false;
                        for j in (self.ip + 1)..self.lines.len() {
                            let pt = strip_comment(&self.lines[j]).trim().to_string();
                            if pt.is_empty() {
                                continue;
                            }
                            if pt.starts_with('!') {
                                has_more = true;
                            }
                            break;
                        }
                        if has_more {
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
                        let mut has_more = false;
                        for j in (self.ip + 1)..self.lines.len() {
                            let pt = strip_comment(&self.lines[j]).trim().to_string();
                            if pt.is_empty() {
                                continue;
                            }
                            if pt.starts_with('^') {
                                has_more = true;
                            }
                            break;
                        }
                        if has_more {
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
        let (start_ip, end_ip, params) =
            self.functions.get(name).cloned().ok_or_else(|| {
                VMError::Str(format!(
                    "Undefined function '{}'. Define it with: F {}(...)",
                    name, name
                ))
            })?;
        if args.len() != params.len() {
            return Err(VMError::Str(format!(
                "Function '{}' expects {} argument(s) ({}), but got {}.",
                name,
                params.len(),
                params.join(", "),
                args.len()
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
            let line_num = self.ip + 1;
            self.ip += 1;

            let stmt = parse_statement(&line).map_err(|e| {
                format_error(line_num, &line, &e)
            })?;

            match self.execute_statement(stmt) {
                Ok(_) => {}
                Err(VMError::Return(_)) => {
                    return Err(format_error(
                        line_num,
                        &line,
                        "R (return) statement used outside of a function. Wrap it in F...~ block.",
                    ));
                }
                Err(VMError::Str(e)) => {
                    if let Some(t_ip) = self.try_stack.pop() {
                        let c_ip = self.block_info[t_ip].matching_else.unwrap();
                        if let Some(var) = Self::parse_c_var(&self.lines[c_ip]) {
                            self.vars.insert(var, Value::Str(e.clone()));
                        }
                        self.ip = c_ip + 1;
                    } else {
                        return Err(format_error(line_num, &line, &e));
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
        eprintln!("{{}}", e);
    }}
}}"#,
        escaped_vul
    );

    let final_code = format!("{}{}", base_code, custom_main);

    let temp_dir = std::env::temp_dir().join("vulpin_build_temp");
    let _ = std::fs::remove_dir_all(&temp_dir);
    std::fs::create_dir_all(temp_dir.join("src")).unwrap();

    std::fs::write(
        temp_dir.join("Cargo.toml"),
        r#"
[package]
name = "Vulpin App"
version = "0.1.1"
edition = "2026"
"#,
    )
    .unwrap();

    std::fs::write(temp_dir.join("src/main.rs"), final_code).unwrap();

    let target_triple = match target_os {
        "windows" => "x86_64-pc-windows-gnu",
        "linux" => "x86_64-unknown-linux-gnu",
        "macos" => "x86_64-apple-darwin",
        _ => "",
    };

    let mut cmd = std::process::Command::new("cargo");
    cmd.arg("build").arg("--release");
    if !target_triple.is_empty() {
        cmd.arg("--target").arg(target_triple);
    }
    cmd.current_dir(&temp_dir);

    println!(
        "Building standalone app for {}...",
        if target_os == "native" {
            "current OS"
        } else {
            target_os
        }
    );
    let status = cmd
        .status()
        .expect("Failed to run cargo. Is Rust installed?");

    if status.success() {
        let exe_name = if target_os == "windows"
            || (target_os == "native" && cfg!(target_os = "windows"))
        {
            "vulpin_app.exe"
        } else {
            "vulpin_app"
        };
        let mut output_path = temp_dir.join("target");
        if !target_triple.is_empty() {
            output_path = output_path.join(target_triple);
        }
        output_path = output_path.join("release").join(exe_name);

        let final_name = std::path::Path::new(vul_file)
            .file_stem()
            .unwrap_or_default()
            .to_str()
            .unwrap_or("app");
        let final_ext = if target_os == "windows"
            || (target_os == "native" && cfg!(target_os = "windows"))
        {
            ".exe"
        } else {
            ""
        };
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
        println!("Vul 0.8beta");
        return;
    } else {
        &args[1]
    };

    let content = match std::fs::read_to_string(filename) {
        Ok(c) => c,
        Err(e) => {
            eprintln!(
                "{}",
                err_red(&format!("Error: Cannot open file '{}': {}", filename, e))
            );
            return;
        }
    };

    let lines: Vec<String> = content.lines().map(|s| s.to_string()).collect();
    let mut vm = VM::new(lines);
    vm.precompute();

    if let Err(e) = vm.run() {
        eprintln!("{}", e);
    }
}
