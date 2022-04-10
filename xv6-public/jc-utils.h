#define LOG_LEVEL_DEBUG 1
#define LOG_LEVEL_VERBOSE 2

#if LOG_LEVEL >= LOG_LEVEL_DEBUG
#define log_d(args...) cprintf(args) 
#else
#define log_d(args...) while (0)
#endif

#if LOG_LEVEL >= LOG_LEVEL_VERBOSE
#define log_v(args...) cprintf(args) 
#else
#define log_v(args...) while (0)
#endif
