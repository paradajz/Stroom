import fs from "node:fs";

/** Write generated C only when changed so repeated checks do not force recompilation. */
export function writeFixture(output, source) {
  if (!fs.existsSync(output) || fs.readFileSync(output, "utf8") !== source) {
    fs.writeFileSync(output, source);
  }
}

/** Append a Unity entry point for a generated, resource-free C case. */
export function unityRunner(name) {
  return `
void setUp(void) {}
void tearDown(void) {}
int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(${name});
    return UNITY_END();
}
`;
}
