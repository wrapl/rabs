from pygments.lexer import RegexLexer, words, include
from pygments.token import *

class MinilangLexer(RegexLexer):
	name = 'Minilang'
	aliases = ['minilang']
	filenames = ['*.mini']
	
	tokens = {
		'root': [
			(words((
				"if", "then", "elseif", "else", "end", "loop",
				"while", "until", "exit", "next", "for", "all",
				"in", "is", "fun", "return", "suspend", "ret",
				"susp", "with", "do", "on", "nil", "and", "or",
				"not", "old", "def", "var"
			), suffix = r'\b'), Name.Builtin),
			(r'-?([0-9]*\.)?[0-9]+((e|E)[0-9]+)?', Number),
			('\"', String, 'string'),
			('\'', String, 'string2'),
			('\(', Operator, 'brackets'),
			('\{', Operator, 'braces'),
			(r':[A-Za-z_]+', Name.Entity),
			(r'--.*\n', Comment),
			(r'\s+', Text),
			(r'[A-Za-z_][A-Za-z0-9_]*', Text),
			(':=', Operator),
			(',', Operator),
			(']', Operator),
			('\[', Operator),
			(r'[!@#$%^&*+=|\\~`/?<>.-]+', Operator)
		],
		'string': [
			('\"', String, '#pop'),
			(r'\\.', String.Escape),
			(r'[^"\\]+', String)
		],
		'string2': [
			('\'', String, '#pop'),
			(r'\\.', String.Escape),
			('{', Operator, 'braces'),
			(r'[^\'\\{]+', String)
		],
		'braces': [
			('}', Operator, '#pop'),
			include('root')
		],
		'brackets': [
			('\)', Operator, '#pop'),
			include('root')
		]
	}