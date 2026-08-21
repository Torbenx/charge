
import {
	CompletionItem,
	CompletionItemKind,
	CompletionList,
	ExtensionContext,
	Position,
	Range,
	TextDocument,
	TextDocumentChangeEvent,
	TextDocumentChangeReason,
	TextEditor,
	languages,
	window,
	workspace
} from 'vscode';

const language = 'chiral';

// Keep in sync with 'symbols' in compiler/verify/language/Parser.h.
// Also need to be listed as 'allowedCharacters' in package.json and 'operators' in chiral.tmLanguage.json.
const operators = [
	{ command: '\\and', symbol: '∧' },
	{ command: '\\or', symbol: '∨' },
	{ command: '\\neg', symbol: '¬' },
	{ command: '\\neq', symbol: '≠' },
];

/**
 * The command being written in front of 'character', or undefined if none begins there.
 * A backslash is no character of the language, so one always begins a command.
 */
export function readCommand(line: string, character: number)
	: { command: string, start: number } | undefined {
	let start = character;
	while (start > 0 && /[A-Za-z]/.test(line[start - 1]))
		start -= 1;
	if (start == 0 || line[start - 1] != '\\')
		return undefined;
	return { command: line.slice(start - 1, character), start: start - 1 };
}

/**
 * The symbol a command written out in full stands for. A command another one continues stands
 * for nothing yet, as the one being written is not known before the next character is read.
 */
export function completedSymbol(command: string): string | undefined {
	const completed = operators.find(operator => operator.command == command);
	if (completed == undefined)
		return undefined;
	const continued = operators.some(operator => operator.command != command
		&& operator.command.startsWith(command));
	if (continued)
		return undefined;
	return completed.symbol;
}

const completionProvider = {
	provideCompletionItems(document: TextDocument, position: Position): CompletionList {
		const written = readCommand(document.lineAt(position).text, position.character);
		if (written == undefined)
			return new CompletionList([], true);

		const range = new Range(position.line, written.start, position.line, position.character);
		// A space accepts an entry only once a letter has narrowed the list down, so that a
		// space after a lone backslash stays the space it was typed as
		const commitCharacters = written.command.length > 1 ? [' ', '\n'] : undefined;

		const items = operators.map(operator => {
			const item = new CompletionItem(operator.command, CompletionItemKind.Operator);
			item.detail = operator.symbol;
			item.insertText = operator.symbol;
			// The range and the text a filter matches both take the backslash in, which the
			// word of the language the position falls into leaves out
			item.range = range;
			item.filterText = operator.command;
			item.commitCharacters = commitCharacters;
			return item;
		});

		// The list is an incomplete one so that every keystroke asks again, which is what
		// decides whether a space accepts an entry
		return new CompletionList(items, true);
	}
};

// The replacement is a change of its own, which is not one the user wrote
let replacing = false;

function replaceCompletedCommands(editor: TextEditor) {
	const replacements: { range: Range, symbol: string }[] = [];
	for (const selection of editor.selections) {
		if (!selection.isEmpty)
			continue;
		const position = selection.active;
		const written = readCommand(editor.document.lineAt(position).text, position.character);
		if (written == undefined)
			continue;
		const symbol = completedSymbol(written.command);
		if (symbol == undefined)
			continue;
		const range = new Range(position.line, written.start, position.line, position.character);
		replacements.push({ range, symbol });
	}
	if (replacements.length == 0)
		return;

	replacing = true;
	const done = () => { replacing = false; };
	// The replacement is an undo step of its own, so undoing it once writes the command back.
	// Nothing is typed by then, so the command is left as it is until it is written again.
	editor.edit(edit => {
		for (const replacement of replacements)
			edit.replace(replacement.range, replacement.symbol);
	}, { undoStopBefore: true, undoStopAfter: true }).then(done, done);
}

function onDidChangeTextDocument(event: TextDocumentChangeEvent) {
	if (replacing)
		return;
	if (event.document.languageId != language)
		return;
	if (event.reason == TextDocumentChangeReason.Undo
		|| event.reason == TextDocumentChangeReason.Redo)
		return;
	const editor = window.activeTextEditor;
	if (editor == undefined || editor.document != event.document)
		return;

	// A command is completed by typing a letter. Anything else is pasted, or written for the
	// user, and is left the way it was written.
	if (event.contentChanges.length == 0)
		return;
	if (!event.contentChanges.every(change => /^[A-Za-z]$/.test(change.text)))
		return;

	replaceCompletedCommands(editor);
}

export function registerUnicodeOperators(context: ExtensionContext) {
	context.subscriptions.push(
		languages.registerCompletionItemProvider(language, completionProvider, '\\'));
	context.subscriptions.push(workspace.onDidChangeTextDocument(onDidChangeTextDocument));
}
