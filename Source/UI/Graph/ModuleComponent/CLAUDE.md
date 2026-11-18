# Module card invariants (Source/UI/Graph/ModuleComponent/)

- **The header's Mute button exists only on a module with a `muted` parameter** (`ModuleBase::hasMuteParameter()`); every use of `muteButton` guards for null, and the Dual I/O button takes the freed slot. → [modules](../../../../docs/modules/modules.md)
