// C64 GrooveGoat - a local language model (Ollama) as the arranger
//
// Ollama runs on the same computer (http://localhost:11434), so nothing
// leaves the computer and no internet is needed once a model is installed.
#ifndef GOLLAMA_H
#define GOLLAMA_H

#define OLLAMA_MAXMODELS 32

extern char ollamahost[64];          // "127.0.0.1:11434" (config)

// the installed models (blocks for at most about 2 seconds); -1 = Ollama not reachable
int ollama_models(char names[][64], int max, char *err, int errsize);
// asks the model in the background; 0 = could not start
int ollama_start(const char *model, const char *prompt);
// 0 still busy, 1 done (*answer: the text, free() it), -1 error (err), 2 nothing running
int ollama_poll(char **answer, char *err, int errsize);
void ollama_cancel(void);
unsigned ollama_seconds(void);       // seconds since the start of the request

#endif
