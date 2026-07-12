#pragma once

namespace stubs
{
// Called only from the launcher timeout path, never while the loader hook is executing.
void FlushLdrLoadTrace();
} // namespace stubs
