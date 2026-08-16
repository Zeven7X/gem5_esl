export function parseYaml(text) {
  if (!window.jsyaml) {
    throw new Error("js-yaml 未正确加载");
  }
  return window.jsyaml.load(text);
}

export function stringifyYaml(value) {
  if (!window.jsyaml) {
    throw new Error("js-yaml 未正确加载");
  }
  return window.jsyaml.dump(value, {
    indent: 2,
    lineWidth: 100,
    noRefs: true,
    sortKeys: false,
    quotingType: "'",
    forceQuotes: false,
  });
}

export async function fingerprint(text) {
  if (window.crypto?.subtle) {
    const bytes = new TextEncoder().encode(text);
    const digest = await window.crypto.subtle.digest("SHA-256", bytes);
    return Array.from(new Uint8Array(digest))
      .map((byte) => byte.toString(16).padStart(2, "0"))
      .join("");
  }

  let hash = 2166136261;
  for (let index = 0; index < text.length; index += 1) {
    hash ^= text.charCodeAt(index);
    hash = Math.imul(hash, 16777619);
  }
  return `fallback-${(hash >>> 0).toString(16)}`;
}

export function downloadText(filename, text, type = "text/yaml") {
  const blob = new Blob([text], { type });
  const url = URL.createObjectURL(blob);
  const anchor = document.createElement("a");
  anchor.href = url;
  anchor.download = filename;
  anchor.click();
  URL.revokeObjectURL(url);
}
