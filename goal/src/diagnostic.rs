use crate::span::SpanInfo;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum DiagnosticLevel {
    Warning,
    Error,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct DiagnosticContext {
    pub message: String,
    pub span: Option<SpanInfo>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct Diagnostic {
    pub level: DiagnosticLevel,
    pub message: String,
    pub span: Option<SpanInfo>,
    pub context: Vec<DiagnosticContext>,
}

impl Diagnostic {
    pub fn error(message: impl Into<String>) -> Self {
        Self {
            level: DiagnosticLevel::Error,
            message: message.into(),
            span: None,
            context: Vec::new(),
        }
    }

    pub fn warning(message: impl Into<String>) -> Self {
        Self {
            level: DiagnosticLevel::Warning,
            message: message.into(),
            span: None,
            context: Vec::new(),
        }
    }

    pub fn with_span(mut self, span: SpanInfo) -> Self {
        self.span = Some(span);
        self
    }

    pub fn with_context(mut self, message: impl Into<String>, span: Option<SpanInfo>) -> Self {
        self.context.push(DiagnosticContext {
            message: message.into(),
            span,
        });
        self
    }
}
