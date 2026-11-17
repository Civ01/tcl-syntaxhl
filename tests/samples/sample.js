#!/usr/bin/env node
/* Sample script */
import { readFile } from 'node:fs/promises';

const pattern = /^[a-z]+\d*$/i;
const half = 10 / 2 / 1;

export async function load(path, retries = 0x3) {
	const text = await readFile(path, 'utf8');
	return `Loaded ${path}: ${text.length} chars, ${ { ok: `yes ${retries}` }.ok }`;
}

class Cache extends Map {
	#hits = 0n;
	get hits() { return this.#hits ?? null; }
}

console.log(pattern.test("abc1"), half, undefined);
