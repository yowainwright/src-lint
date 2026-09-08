import { postEntry } from "../billing/internal/ledger.ts";
import { listEntries } from "../billing/api/index.ts";

postEntry();
listEntries();
