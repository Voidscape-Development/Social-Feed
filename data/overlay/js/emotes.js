/* Third-party emotes (BetterTTV, FrankerFaceZ, 7TV) and Twitch pronouns, loaded per Twitch
 * channel id and cached for the page's lifetime. All lookups fail soft: a service being down
 * just means its emotes show as text. */
(function () {
	'use strict';

	const SF = window.SF;
	const channelSets = new Map(); /* key -> Promise<Map<code, emote>> */

	async function getJson(url) {
		const response = await fetch(url);
		if (!response.ok)
			throw new Error(url + ' -> ' + response.status);
		return response.json();
	}

	function bttvEmote(e) {
		return { code: e.code, url: `https://cdn.betterttv.net/emote/${e.id}/2x`, source: 'bttv' };
	}

	function ffzEmote(e) {
		const images = e.images || {};
		return { code: e.code, url: images['2x'] || images['1x'], source: 'ffz' };
	}

	function sevenTvEmote(e) {
		const host = (e.data && e.data.host) || {};
		const files = host.files || [];
		const file = files.find((f) => f.name === '2x.webp') || files.find((f) => /2x/.test(f.name)) || files[0];
		if (!host.url || !file)
			return null;
		const flags = (e.data && e.data.flags) || 0;
		return {
			code: e.name,
			url: 'https:' + host.url.replace(/^https?:/, '') + '/' + file.name,
			source: '7tv',
			zeroWidth: (flags & 256) !== 0,
		};
	}

	async function load(key, loaders) {
		const map = new Map();
		const results = await Promise.allSettled(loaders.map((fn) => fn()));
		for (const result of results) {
			if (result.status !== 'fulfilled')
				continue;
			for (const emote of result.value) {
				if (emote && emote.code && emote.url)
					map.set(emote.code, emote);
			}
		}
		return map;
	}

	function loadersFor(channelId, options) {
		const list = [];
		if (!channelId) {
			if (options.bttv)
				list.push(async () => (await getJson('https://api.betterttv.net/3/cached/emotes/global')).map(bttvEmote));
			if (options.ffz)
				list.push(async () =>
					(await getJson('https://api.betterttv.net/3/cached/frankerfacez/emotes/global')).map(ffzEmote));
			if (options.sevenTv)
				list.push(async () =>
					((await getJson('https://7tv.io/v3/emote-sets/global')).emotes || []).map(sevenTvEmote));
			return list;
		}
		if (options.bttv)
			list.push(async () => {
				const data = await getJson('https://api.betterttv.net/3/cached/users/twitch/' + channelId);
				return [...(data.channelEmotes || []), ...(data.sharedEmotes || [])].map(bttvEmote);
			});
		if (options.ffz)
			list.push(async () =>
				(await getJson('https://api.betterttv.net/3/cached/frankerfacez/users/twitch/' + channelId)).map(
					ffzEmote));
		if (options.sevenTv)
			list.push(async () => {
				const data = await getJson('https://7tv.io/v3/users/twitch/' + channelId);
				return ((data.emote_set && data.emote_set.emotes) || []).map(sevenTvEmote);
			});
		return list;
	}

	function optionsKey(options) {
		return [options.bttv, options.ffz, options.sevenTv].map((v) => (v ? 1 : 0)).join('');
	}

	function sets(channelId, options) {
		const key = (channelId || 'global') + ':' + optionsKey(options);
		if (!channelSets.has(key))
			channelSets.set(key, load(key, loadersFor(channelId, options)));
		return channelSets.get(key);
	}

	SF.emotes = {
		/* Resolves once the global and channel sets are loaded (or failed). */
		async forChannel(channelId, options) {
			const [global, channel] = await Promise.all([sets('', options), sets(channelId, options)]);
			return { get: (code) => channel.get(code) || global.get(code) };
		},
	};

	/* ---- pronouns (api.pronouns.alejo.io) ---- */

	let pronounNames = null;
	const pronounCache = new Map();

	async function pronounTable() {
		if (!pronounNames) {
			pronounNames = getJson('https://api.pronouns.alejo.io/v1/pronouns').catch(() => ({}));
		}
		return pronounNames;
	}

	SF.pronouns = async function (login) {
		if (!login)
			return '';
		if (!pronounCache.has(login)) {
			pronounCache.set(login, (async () => {
				try {
					const [table, user] = await Promise.all([
						pronounTable(),
						getJson('https://api.pronouns.alejo.io/v1/users/' + encodeURIComponent(login)),
					]);
					const main = table[user.pronoun_id];
					if (!main)
						return '';
					const alt = user.alt_pronoun_id && table[user.alt_pronoun_id];
					if (alt)
						return `${main.subject}/${alt.subject}`;
					return main.singular ? main.subject : `${main.subject}/${main.object}`;
				} catch (err) {
					return '';
				}
			})());
		}
		return pronounCache.get(login);
	};
})();
