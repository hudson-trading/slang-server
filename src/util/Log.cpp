// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT

#include "util/Log.h"

thread_local FILE* server::logging::detail::output = stderr;
