interface Env {
  TELEMETRY: AnalyticsEngineDataset;
}

// Mirrors the slug list in download-counter/src/index.ts — kept in sync
// manually since each Worker is deployed independently.
const PLUGIN_SLUGS = [
  "caverns",
  "damage",
  "corrosion",
  "flux",
  "alloy",
  "gradient",
  "shields",
  "intruder",
  "aura",
  "concrete",
  "strike",
] as const;
type PluginSlug = (typeof PLUGIN_SLUGS)[number];

function isPluginSlug(value: unknown): value is PluginSlug {
  return (
    typeof value === "string" &&
    (PLUGIN_SLUGS as readonly string[]).includes(value)
  );
}

const EVENT_TYPES = ["launch", "preset"] as const;
type EventType = (typeof EVENT_TYPES)[number];

function isEventType(value: unknown): value is EventType {
  return (
    typeof value === "string" && (EVENT_TYPES as readonly string[]).includes(value)
  );
}

const MAX_FIELD_LENGTH = 200;

function isShortString(value: unknown): value is string {
  return (
    typeof value === "string" && value.length > 0 && value.length <= MAX_FIELD_LENGTH
  );
}

function isOptionalShortString(value: unknown): value is string | undefined {
  return value === undefined || isShortString(value);
}

interface TelemetryEvent {
  type: EventType;
  plugin: PluginSlug;
  version: string;
  os: string;
  installId: string;
  host?: string;
  preset?: string;
}

function parseEvent(body: unknown): TelemetryEvent | null {
  if (typeof body !== "object" || body === null) return null;
  const b = body as Record<string, unknown>;

  if (!isEventType(b.type)) return null;
  if (!isPluginSlug(b.plugin)) return null;
  if (!isShortString(b.version)) return null;
  if (!isShortString(b.os)) return null;
  if (!isShortString(b.installId)) return null;
  if (!isOptionalShortString(b.host)) return null;

  if (b.type === "preset" && !isShortString(b.preset)) return null;
  if (b.type === "launch" && !isOptionalShortString(b.preset)) return null;

  return {
    type: b.type,
    plugin: b.plugin,
    version: b.version,
    os: b.os,
    installId: b.installId,
    host: b.host as string | undefined,
    preset: b.preset as string | undefined,
  };
}

export default {
  async fetch(request: Request, env: Env): Promise<Response> {
    const url = new URL(request.url);

    if (url.pathname === "/health") {
      return new Response("ok");
    }

    if (url.pathname === "/v1/event" && request.method === "POST") {
      let body: unknown;
      try {
        body = await request.json();
      } catch {
        return new Response("Invalid JSON", { status: 400 });
      }

      const event = parseEvent(body);
      if (!event) {
        return new Response("Invalid event", { status: 400 });
      }

      env.TELEMETRY.writeDataPoint({
        blobs: [
          event.type,
          event.plugin,
          event.version,
          event.os,
          event.host ?? "",
          event.preset ?? "",
        ],
        doubles: [1],
        indexes: [event.installId],
      });

      return new Response(null, { status: 204 });
    }

    return new Response("Not found", { status: 404 });
  },
} satisfies ExportedHandler<Env>;
