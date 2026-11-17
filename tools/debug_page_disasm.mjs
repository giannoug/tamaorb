// SPDX-License-Identifier: GPL-3.0-or-later
// Test helper: runs debug.html's disassembler (the code between DISASM-BEGIN/END) on [word, pc, prev] cases from
// stdin and prints the texts as JSON. Usage: node tools/debug_page_disasm.mjs components/web/page/debug.html
import { readFileSync } from "node:fs";

const page = readFileSync(process.argv[2], "utf8");
const m = page.match(/\/\*DISASM-BEGIN\*\/([\s\S]*?)\/\*DISASM-END\*\//);
if (!m) throw new Error("DISASM block missing");
const disasm = new Function(m[1] + "\nreturn disasm;")();
const cases = JSON.parse(readFileSync(0, "utf8"));
process.stdout.write(JSON.stringify(cases.map(([w, pc, prev]) => disasm(w, pc, prev))));
