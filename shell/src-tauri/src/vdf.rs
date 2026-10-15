//! Leitor do formato de texto VDF da Steam (`libraryfolders.vdf`, `appmanifest_*.acf`).
//!
//! ```text
//! "AppState"
//! {
//!     "appid"   "730"
//!     "name"    "Counter-Strike 2"
//! }
//! ```

#[derive(Debug, Clone, PartialEq)]
pub enum Value {
    Text(String),
    Map(Vec<(String, Value)>),
}

impl Value {
    /// Valor de uma chave (sem diferenciar maiúsculas, como a Steam faz).
    pub fn get(&self, key: &str) -> Option<&Value> {
        match self {
            Value::Map(entries) => entries.iter().find(|(k, _)| k.eq_ignore_ascii_case(key)).map(|(_, v)| v),
            Value::Text(_) => None,
        }
    }

    pub fn text(&self, key: &str) -> Option<&str> {
        match self.get(key)? {
            Value::Text(text) => Some(text),
            Value::Map(_) => None,
        }
    }

    pub fn entries(&self) -> &[(String, Value)] {
        match self {
            Value::Map(entries) => entries,
            Value::Text(_) => &[],
        }
    }
}

struct Parser<'a> {
    chars: std::iter::Peekable<std::str::Chars<'a>>,
}

enum Token {
    Text(String),
    Open,
    Close,
}

impl Parser<'_> {
    fn token(&mut self) -> Option<Token> {
        loop {
            let c = self.chars.next()?;
            match c {
                c if c.is_whitespace() => continue,
                '{' => return Some(Token::Open),
                '}' => return Some(Token::Close),
                '/' if self.chars.peek() == Some(&'/') => {
                    for c in self.chars.by_ref() {
                        if c == '\n' {
                            break;
                        }
                    }
                }
                '"' => {
                    let mut text = String::new();
                    while let Some(c) = self.chars.next() {
                        match c {
                            '"' => break,
                            '\\' => match self.chars.next() {
                                Some('n') => text.push('\n'),
                                Some('t') => text.push('\t'),
                                Some(other) => text.push(other),
                                None => break,
                            },
                            c => text.push(c),
                        }
                    }
                    return Some(Token::Text(text));
                }
                c => {
                    // Texto sem aspas (raro): vai até o próximo espaço.
                    let mut text = c.to_string();
                    while let Some(&c) = self.chars.peek() {
                        if c.is_whitespace() || c == '{' || c == '}' {
                            break;
                        }
                        text.push(c);
                        self.chars.next();
                    }
                    return Some(Token::Text(text));
                }
            }
        }
    }

    /// Lê pares chave/valor até o `}` (ou o fim do arquivo).
    fn map(&mut self) -> Vec<(String, Value)> {
        let mut entries = Vec::new();
        while let Some(token) = self.token() {
            let key = match token {
                Token::Text(key) => key,
                Token::Close => break,
                Token::Open => continue,
            };
            match self.token() {
                Some(Token::Text(value)) => entries.push((key, Value::Text(value))),
                Some(Token::Open) => entries.push((key, Value::Map(self.map()))),
                Some(Token::Close) | None => break,
            }
        }
        entries
    }
}

pub fn parse(text: &str) -> Value {
    Value::Map(Parser { chars: text.chars().peekable() }.map())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn reads_an_app_manifest() {
        let doc = parse(
            r#""AppState"
            {
                "appid"		"730"
                "name"		"Counter-Strike 2"
                "StateFlags"		"4"
                "UserConfig"
                {
                    "language"		"brazilian"
                }
            }"#,
        );
        let app = doc.get("AppState").unwrap();
        assert_eq!(app.text("appid"), Some("730"));
        assert_eq!(app.text("NAME"), Some("Counter-Strike 2"));
        assert_eq!(app.get("UserConfig").unwrap().text("language"), Some("brazilian"));
    }

    #[test]
    fn handles_escapes_and_comments() {
        let doc = parse("// comentário\n\"a\" \"C:\\\\Jogos\\\\x\"\n\"b\" { }");
        assert_eq!(doc.text("a"), Some("C:\\Jogos\\x"));
        assert_eq!(doc.get("b"), Some(&Value::Map(vec![])));
    }
}
