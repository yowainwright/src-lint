const first = import(
  "../billing/internal/lazy.ts"
);
const second = require(
  "../billing/internal/ledger.ts"
);
const third = import /* bridge */ ("../billing/internal/other.ts");
const fourth = require /* bridge */ ("../billing/internal/fourth.ts");

void first;
void second;
void third;
void fourth;
