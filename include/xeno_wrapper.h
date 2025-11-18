#ifndef XENO_WRAPPER_H
#define XENO_WRAPPER_H

#ifdef __cplusplus
extern "C" {
#endif

/* Entrypoint (exported for Eden) */
void xeno_init(void);

/* Force a manual deep dump (callable via JNI or dlsym) */
void xeno_force_dump(void);

/* Small helper to flush logs */
void xeno_flush(void);

#ifdef __cplusplus
}
#endif

#endif /* XENO_WRAPPER_H */
