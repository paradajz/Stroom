import { cachePointInvariants } from "./point_invariants.mjs";
// Static descriptors and separate equation scopes for classic custom objects.
export const objectFields = [
  "x",
  "y",
  "r",
  "g",
  "b",
  "a",
  "r2",
  "g2",
  "b2",
  "a2",
  "rad",
  "ang",
  "tex_zoom",
  "tex_ang",
  "sides",
  "additive",
  "textured",
  "thick",
  "instances",
  "border_r",
  "border_g",
  "border_b",
  "border_a",
  "samples",
  "sep",
  "spectrum",
  "dots",
  "scaling",
  "smoothing",
];
export const frameInputs = [
  "time",
  "fps",
  "frame",
  "bass",
  "mid",
  "treb",
  "bass_att",
  "mid_att",
  "treb_att",
  "progress",
];
export const qVariables = Array.from({ length: 32 }, (_, i) => "q" + (i + 1));
export const tVariables = Array.from({ length: 8 }, (_, i) => "t" + (i + 1));
// Contiguous frame registers copied into the custom-wave point state.
export const objectPointRegisters = [...qVariables, ...tVariables];
export const objectBuiltin = [
  ...objectFields,
  ...frameInputs,
  ...objectPointRegisters,
  "sample",
  "value1",
  "value2",
  "instance",
];
export const objectLimits = { shape: 4, wave: 4 };
export const OBJECT_VARIABLES = 192;
export const OBJECT_INSTANCES = 16;
export function compileObjects(fields, limits, Parser, numeric, code) {
  const result = [];
  for (const [type, limit] of Object.entries(objectLimits))
    for (let index = 0; index < limit; index++) {
      const prefix = type + "code_" + index + "_";
      const enabled = numeric(fields, prefix + "enabled");
      if (![0, 1].includes(enabled)) throw Error("invalid object enable flag");
      if (!enabled) continue;
      const shape = type === "shape";
      const mapping = shape
        ? { thick: "thickOutline", instances: "num_inst" }
        : {
            spectrum: "bSpectrum",
            dots: "bUseDots",
            thick: "bDrawThick",
            additive: "bAdditive",
          };
      const supported = shape
        ? [
            "x",
            "y",
            "r",
            "g",
            "b",
            "a",
            "r2",
            "g2",
            "b2",
            "a2",
            "rad",
            "ang",
            "tex_zoom",
            "tex_ang",
            "sides",
            "additive",
            "textured",
            "thick",
            "instances",
            "border_r",
            "border_g",
            "border_b",
            "border_a",
          ]
        : [
            "samples",
            "sep",
            "spectrum",
            "dots",
            "thick",
            "additive",
            "scaling",
            "smoothing",
            "r",
            "g",
            "b",
            "a",
          ];
      const known = new Set([
        "enabled",
        ...supported.map((k) => mapping[k] || k),
      ]);
      for (const key of fields.keys())
        if (key.startsWith(prefix) && !known.has(key.slice(prefix.length)))
          throw Error("unsupported custom " + type + " field " + key);
      const defaults = {
        x: 0.5,
        y: 0.5,
        r: 1,
        g: shape ? 0 : 1,
        b: shape ? 0 : 1,
        a: 1,
        g2: 1,
        rad: 0.1,
        tex_zoom: 1,
        sides: 4,
        instances: 1,
        border_r: 1,
        border_g: 1,
        border_b: 1,
        border_a: 0.1,
        samples: 512,
        scaling: 1,
        smoothing: 0.5,
      };
      const values = objectFields.map((k) =>
        supported.includes(k)
          ? numeric(fields, prefix + (mapping[k] || k), defaults[k] || 0)
          : 0,
      );
      const value = (k) => values[objectFields.indexOf(k)];
      for (const key of shape
        ? ["additive", "textured", "thick"]
        : ["spectrum", "dots", "thick", "additive"])
        if (![0, 1].includes(value(key)))
          throw Error("invalid custom " + key + " flag");
      if (
        shape &&
        (!Number.isInteger(value("instances")) ||
          value("instances") < 1 ||
          value("instances") > OBJECT_INSTANCES)
      )
        throw Error("custom shape instance budget");
      if (!shape && (value("smoothing") < 0 || value("smoothing") > 1))
        throw Error("invalid custom wave smoothing");
      const vars = new Map(objectBuiltin.map((k, i) => [k, i]));
      const compiled = [];
      for (const stage of shape
        ? ["init", "per_frame"]
        : ["init", "per_frame", "per_point"]) {
        const readonly = ["instance"];
        if (!shape)
          readonly.push(
            "spectrum",
            "dots",
            "thick",
            "additive",
            "scaling",
            "smoothing",
            "sep",
          );
        const parser = new Parser(
          code(fields, type + "_" + index + "_" + stage),
          vars,
          { object: true, readonly },
        );
        let c = parser.parse();
        if (stage === "per_point")
          c = cachePointInvariants(c, vars, OBJECT_VARIABLES);
        if (
          parser.ops >
          (stage === "per_point"
            ? limits.vertexOperations
            : limits.frameOperations)
        )
          throw Error("custom " + stage + " operation budget");
        if (vars.size > OBJECT_VARIABLES)
          throw Error("custom object variable budget");
        compiled.push({ c, operations: parser.ops });
      }
      result.push({
        type,
        index,
        defaults: values,
        compiled,
        variables: [...vars.keys()],
      });
    }
  for (const [key, value] of fields) {
    const match = /^(wave|shape)code_(\d+)_enabled$/.exec(key);
    if (
      match &&
      Number(match[2]) >= objectLimits[match[1]] &&
      Number(value) !== 0
    )
      throw Error("custom object count budget");
  }
  return result;
}
