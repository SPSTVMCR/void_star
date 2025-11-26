#pragma once
#include <Arduino.h>

/*
  ai_state.h
  Shared AI job state for asynchronous Gemini requests.
  All boolean flags marked volatile since they are read by both
  the AI task and the web server (different contexts).
*/

struct AIJob {
  volatile bool running = false;
  volatile bool done    = false;
  volatile bool ok      = false;
  volatile bool canceled = false;

  // Prompt requested by user
  String prompt;

  // Applied actions summary OR raw model JSON (truncated) if error
  String appliedSummary;
  String modelJsonSnippet;

  // Error message (empty if ok)
  String error;

  // Timestamp (millis) when job started; used for rate limiting
  unsigned long startedMs = 0;
};

extern AIJob g_aiJob;

// Rate limiting config (milliseconds between job starts)
#ifndef AI_MIN_INTERVAL_MS
#define AI_MIN_INTERVAL_MS 4000UL
#endif

// Timeout for a single AI job (safety, ms)
#ifndef AI_JOB_TIMEOUT_MS
#define AI_JOB_TIMEOUT_MS 30000UL
#endif

// Maximum raw model JSON snippet stored (for debugging)
#ifndef AI_MODEL_SNIPPET_MAX
#define AI_MODEL_SNIPPET_MAX 512
#endif

// Log helper (centralized enable)
#ifndef AI_DEBUG_LOG
#define AI_DEBUG_LOG 1
#endif

inline void aiLog(const char* tag, const String& msg) {
#if AI_DEBUG_LOG
  Serial.printf("[AI-%s] %s\n", tag, msg.c_str());
#endif
}