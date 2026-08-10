#pragma once

namespace Pascal
{
enum class TokenType
{
    // The keywords.
    KwProgram,
    KwVar,
    KwConst,
    KwBegin,
    KwEnd,
    KwInteger,
    KwReal,
    KwBoolean,
    KwChar,
    KwArray,
    KwOf,
    KwIf,
    KwThen,
    KwElse,
    KwWhile,
    KwDo,
    KwFor,
    KwTo,
    KwDownto,
    KwRepeat,
    KwUntil,
    KwCase,
    KwProcedure,
    KwFunction,
    KwDiv,
    KwMod,
    KwAnd,
    KwOr,
    KwNot,
    KwWriteln,

    // Identifiers and literals.
    Identifier,
    IntLiteral,
    RealLiteral,
    StringLiteral, // Also used for char literals ('x'); type decided by length.
    BooleanLiteral,

    // The operators.
    Assign,    // :=
    Plus,      // +
    Minus,     // -
    Star,      // *
    Slash,     // /
    Equal,     // =
    NotEqual,  // <>
    LessThan,  // <
    LessEq,    // <=
    GreatThan, // >
    GreatEq,   // >=

    // Delimiters.
    Semicolon, // ;
    Colon,     // :
    Comma,     // ,
    Dot,       // .
    DotDot,    // ..
    LParen,    // (
    RParen,    // )
    LBracket,  // [
    RBracket,  // ]

    // Special.
    Eof,
    Unknown
};
} // namespace Pascal
