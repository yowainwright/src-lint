import { postEntry } from "../billing/private/ledger.ts";
import { listEntries } from "../billing/api/index.ts";

postEntry();
listEntries();
