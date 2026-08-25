interface Env {
  DOWNLOAD_COUNTS: KVNamespace;
  ADMIN_TOKEN: string;
}

const GITHUB_BASE = "https://github.com/mradambeck/audio-plugins/releases/latest/download";

// Mirrors the slug list sync-site-versions.yml derives from each plugin's
// project(<Name> VERSION ...) — kept hardcoded here too since this Worker
// has no access to the main repo's CMakeLists.txt at request time.
const PLUGIN_SLUGS = [
  "caverns",
  "damage",
  "corrosion",
  "flux",
  "alloy",
  "gradient",
  "shields",
] as const;
type PluginSlug = (typeof PLUGIN_SLUGS)[number];

const FORMAT_SUFFIXES = {
  pkg: "-Installer.pkg",
  au: "-AU.zip",
  vst3: "-VST3.zip",
} as const;
type Format = keyof typeof FORMAT_SUFFIXES;

function isPluginSlug(value: string): value is PluginSlug {
  return (PLUGIN_SLUGS as readonly string[]).includes(value);
}

function isFormat(value: string): value is Format {
  return value in FORMAT_SUFFIXES;
}

function capitalize(slug: string): string {
  return slug[0].toUpperCase() + slug.slice(1);
}

type PluginCounts = Partial<Record<Format, number>>;

async function incrementCount(env: Env, key: string, field: string): Promise<void> {
  const existing = (await env.DOWNLOAD_COUNTS.get<Record<string, number>>(key, "json")) ?? {};
  existing[field] = (existing[field] ?? 0) + 1;
  await env.DOWNLOAD_COUNTS.put(key, JSON.stringify(existing));
}

export default {
  async fetch(request: Request, env: Env, ctx: ExecutionContext): Promise<Response> {
    const url = new URL(request.url);
    const parts = url.pathname.split("/").filter(Boolean);

    if (parts[0] === "dl" && parts[1] === "all" && parts.length === 2) {
      ctx.waitUntil(
        incrementCount(env, "counts:all", "count").catch((err) =>
          console.error("count increment failed", err),
        ),
      );
      return Response.redirect(`${GITHUB_BASE}/WildJagPlugins-Installer.pkg`, 302);
    }

    if (parts[0] === "dl" && parts.length === 3) {
      const [, slug, format] = parts;
      if (isPluginSlug(slug) && isFormat(format)) {
        ctx.waitUntil(
          incrementCount(env, `counts:${slug}`, format).catch((err) =>
            console.error("count increment failed", err),
          ),
        );
        const filename = `${capitalize(slug)}${FORMAT_SUFFIXES[format]}`;
        return Response.redirect(`${GITHUB_BASE}/${filename}`, 302);
      }
    }

    if (url.pathname === "/stats") {
      const token = url.searchParams.get("token");
      if (!token || token !== env.ADMIN_TOKEN) {
        return new Response("Unauthorized", { status: 401 });
      }

      const keys = await env.DOWNLOAD_COUNTS.list({ prefix: "counts:" });
      const stats: Record<string, PluginCounts | Record<string, number>> = {};
      for (const { name } of keys.keys) {
        const value = await env.DOWNLOAD_COUNTS.get<Record<string, number>>(name, "json");
        stats[name.replace(/^counts:/, "")] = value ?? {};
      }
      return Response.json(stats);
    }

    return new Response("Not found", { status: 404 });
  },
} satisfies ExportedHandler<Env>;
