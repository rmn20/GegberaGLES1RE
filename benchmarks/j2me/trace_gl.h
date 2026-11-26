#ifndef BENCH_TRACE_GL_H
#define BENCH_TRACE_GL_H
#include <GLES/gl.h>
#ifdef __cplusplus
extern "C" {
#endif
void bench_trace_mark(void);
void bench_trace_frame_end(void);
void bench_trace_source(const char *file);
void GL_APIENTRY trace_glReadPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, GLvoid *pixels);
void GL_APIENTRY trace_glAlphaFunc(GLenum func, GLclampf ref);
void GL_APIENTRY trace_glColor4f(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha);
void GL_APIENTRY trace_glDepthRangef(GLclampf zNear, GLclampf zFar);
void GL_APIENTRY trace_glFogf(GLenum pname, GLfloat param);
void GL_APIENTRY trace_glLightModelf(GLenum pname, GLfloat param);
void GL_APIENTRY trace_glLightModelfv(GLenum pname, const GLfloat *params);
void GL_APIENTRY trace_glLightf(GLenum light, GLenum pname, GLfloat param);
void GL_APIENTRY trace_glLightfv(GLenum light, GLenum pname, const GLfloat *params);
void GL_APIENTRY trace_glLoadMatrixf(const GLfloat *m);
void GL_APIENTRY trace_glMaterialf(GLenum face, GLenum pname, GLfloat param);
void GL_APIENTRY trace_glMaterialfv(GLenum face, GLenum pname, const GLfloat *params);
void GL_APIENTRY trace_glMultMatrixf(const GLfloat *m);
void GL_APIENTRY trace_glPolygonOffset(GLfloat factor, GLfloat units);
void GL_APIENTRY trace_glScalef(GLfloat x, GLfloat y, GLfloat z);
void GL_APIENTRY trace_glTexEnvfv(GLenum target, GLenum pname, const GLfloat *params);
void GL_APIENTRY trace_glTranslatef(GLfloat x, GLfloat y, GLfloat z);
void GL_APIENTRY trace_glActiveTexture(GLenum texture);
void GL_APIENTRY trace_glBindTexture(GLenum target, GLuint texture);
void GL_APIENTRY trace_glBlendFunc(GLenum sfactor, GLenum dfactor);
void GL_APIENTRY trace_glClear(GLbitfield mask);
void GL_APIENTRY trace_glClearColorx(GLclampx red, GLclampx green, GLclampx blue, GLclampx alpha);
void GL_APIENTRY trace_glClearDepthx(GLclampx depth);
void GL_APIENTRY trace_glClientActiveTexture(GLenum texture);
void GL_APIENTRY trace_glColor4x(GLfixed red, GLfixed green, GLfixed blue, GLfixed alpha);
void GL_APIENTRY trace_glColorMask(GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha);
void GL_APIENTRY trace_glColorPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer);
void GL_APIENTRY trace_glCopyTexImage2D(GLenum target, GLint level, GLenum internalformat, GLint x, GLint y, GLsizei width, GLsizei height, GLint border);
void GL_APIENTRY trace_glCullFace(GLenum mode);
void GL_APIENTRY trace_glDeleteTextures(GLsizei n, const GLuint *textures);
void GL_APIENTRY trace_glDepthFunc(GLenum func);
void GL_APIENTRY trace_glDepthMask(GLboolean flag);
void GL_APIENTRY trace_glDisable(GLenum cap);
void GL_APIENTRY trace_glDisableClientState(GLenum array);
void GL_APIENTRY trace_glDrawArrays(GLenum mode, GLint first, GLsizei count);
void GL_APIENTRY trace_glDrawElements(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices);
void GL_APIENTRY trace_glEnable(GLenum cap);
void GL_APIENTRY trace_glEnableClientState(GLenum array);
void GL_APIENTRY trace_glFinish(void);
void GL_APIENTRY trace_glFogxv(GLenum pname, const GLfixed *params);
void GL_APIENTRY trace_glFrontFace(GLenum mode);
void GL_APIENTRY trace_glHint(GLenum target, GLenum mode);
void GL_APIENTRY trace_glLoadIdentity(void);
void GL_APIENTRY trace_glMatrixMode(GLenum mode);
void GL_APIENTRY trace_glNormalPointer(GLenum type, GLsizei stride, const GLvoid *pointer);
void GL_APIENTRY trace_glOrthox(GLfixed left, GLfixed right, GLfixed bottom, GLfixed top, GLfixed zNear, GLfixed zFar);
void GL_APIENTRY trace_glPixelStorei(GLenum pname, GLint param);
void GL_APIENTRY trace_glPopMatrix(void);
void GL_APIENTRY trace_glPushMatrix(void);
void GL_APIENTRY trace_glScissor(GLint x, GLint y, GLsizei width, GLsizei height);
void GL_APIENTRY trace_glShadeModel(GLenum mode);
void GL_APIENTRY trace_glTexCoordPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer);
void GL_APIENTRY trace_glTexEnvx(GLenum target, GLenum pname, GLfixed param);
void GL_APIENTRY trace_glTexImage2D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const GLvoid *pixels);
void GL_APIENTRY trace_glTexParameterx(GLenum target, GLenum pname, GLfixed param);
void GL_APIENTRY trace_glVertexPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer);
void GL_APIENTRY trace_glViewport(GLint x, GLint y, GLsizei width, GLsizei height);
void GL_APIENTRY trace_glCompressedTexImage2D(GLenum target, GLint level, GLenum internalformat, GLsizei width, GLsizei height, GLint border, GLsizei imageSize, const GLvoid *data);
void GL_APIENTRY trace_glTexParameteri(GLenum target, GLenum pname, GLint param);
void GL_APIENTRY trace_glTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid *pixels);
#ifdef __cplusplus
}
#endif
#define glAlphaFunc(...) (bench_trace_source(__FILE__), trace_glAlphaFunc(__VA_ARGS__))
#define glColor4f(...) (bench_trace_source(__FILE__), trace_glColor4f(__VA_ARGS__))
#define glDepthRangef(...) (bench_trace_source(__FILE__), trace_glDepthRangef(__VA_ARGS__))
#define glFogf(...) (bench_trace_source(__FILE__), trace_glFogf(__VA_ARGS__))
#define glLightModelf(...) (bench_trace_source(__FILE__), trace_glLightModelf(__VA_ARGS__))
#define glLightModelfv(...) (bench_trace_source(__FILE__), trace_glLightModelfv(__VA_ARGS__))
#define glLightf(...) (bench_trace_source(__FILE__), trace_glLightf(__VA_ARGS__))
#define glLightfv(...) (bench_trace_source(__FILE__), trace_glLightfv(__VA_ARGS__))
#define glLoadMatrixf(...) (bench_trace_source(__FILE__), trace_glLoadMatrixf(__VA_ARGS__))
#define glMaterialf(...) (bench_trace_source(__FILE__), trace_glMaterialf(__VA_ARGS__))
#define glMaterialfv(...) (bench_trace_source(__FILE__), trace_glMaterialfv(__VA_ARGS__))
#define glMultMatrixf(...) (bench_trace_source(__FILE__), trace_glMultMatrixf(__VA_ARGS__))
#define glPolygonOffset(...) (bench_trace_source(__FILE__), trace_glPolygonOffset(__VA_ARGS__))
#define glScalef(...) (bench_trace_source(__FILE__), trace_glScalef(__VA_ARGS__))
#define glTexEnvfv(...) (bench_trace_source(__FILE__), trace_glTexEnvfv(__VA_ARGS__))
#define glTranslatef(...) (bench_trace_source(__FILE__), trace_glTranslatef(__VA_ARGS__))
#define glActiveTexture(...) (bench_trace_source(__FILE__), trace_glActiveTexture(__VA_ARGS__))
#define glBindTexture(...) (bench_trace_source(__FILE__), trace_glBindTexture(__VA_ARGS__))
#define glBlendFunc(...) (bench_trace_source(__FILE__), trace_glBlendFunc(__VA_ARGS__))
#define glClear(...) (bench_trace_source(__FILE__), trace_glClear(__VA_ARGS__))
#define glClearColorx(...) (bench_trace_source(__FILE__), trace_glClearColorx(__VA_ARGS__))
#define glClearDepthx(...) (bench_trace_source(__FILE__), trace_glClearDepthx(__VA_ARGS__))
#define glClientActiveTexture(...) (bench_trace_source(__FILE__), trace_glClientActiveTexture(__VA_ARGS__))
#define glColor4x(...) (bench_trace_source(__FILE__), trace_glColor4x(__VA_ARGS__))
#define glColorMask(...) (bench_trace_source(__FILE__), trace_glColorMask(__VA_ARGS__))
#define glColorPointer(...) (bench_trace_source(__FILE__), trace_glColorPointer(__VA_ARGS__))
#define glCopyTexImage2D(...) (bench_trace_source(__FILE__), trace_glCopyTexImage2D(__VA_ARGS__))
#define glCullFace(...) (bench_trace_source(__FILE__), trace_glCullFace(__VA_ARGS__))
#define glDeleteTextures(...) (bench_trace_source(__FILE__), trace_glDeleteTextures(__VA_ARGS__))
#define glDepthFunc(...) (bench_trace_source(__FILE__), trace_glDepthFunc(__VA_ARGS__))
#define glDepthMask(...) (bench_trace_source(__FILE__), trace_glDepthMask(__VA_ARGS__))
#define glDisable(...) (bench_trace_source(__FILE__), trace_glDisable(__VA_ARGS__))
#define glDisableClientState(...) (bench_trace_source(__FILE__), trace_glDisableClientState(__VA_ARGS__))
#define glDrawArrays(...) (bench_trace_source(__FILE__), trace_glDrawArrays(__VA_ARGS__))
#define glDrawElements(...) (bench_trace_source(__FILE__), trace_glDrawElements(__VA_ARGS__))
#define glEnable(...) (bench_trace_source(__FILE__), trace_glEnable(__VA_ARGS__))
#define glEnableClientState(...) (bench_trace_source(__FILE__), trace_glEnableClientState(__VA_ARGS__))
#define glFinish(...) (bench_trace_source(__FILE__), trace_glFinish(__VA_ARGS__))
#define glFogxv(...) (bench_trace_source(__FILE__), trace_glFogxv(__VA_ARGS__))
#define glFrontFace(...) (bench_trace_source(__FILE__), trace_glFrontFace(__VA_ARGS__))
#define glHint(...) (bench_trace_source(__FILE__), trace_glHint(__VA_ARGS__))
#define glLoadIdentity(...) (bench_trace_source(__FILE__), trace_glLoadIdentity(__VA_ARGS__))
#define glMatrixMode(...) (bench_trace_source(__FILE__), trace_glMatrixMode(__VA_ARGS__))
#define glNormalPointer(...) (bench_trace_source(__FILE__), trace_glNormalPointer(__VA_ARGS__))
#define glOrthox(...) (bench_trace_source(__FILE__), trace_glOrthox(__VA_ARGS__))
#define glPixelStorei(...) (bench_trace_source(__FILE__), trace_glPixelStorei(__VA_ARGS__))
#define glPopMatrix(...) (bench_trace_source(__FILE__), trace_glPopMatrix(__VA_ARGS__))
#define glPushMatrix(...) (bench_trace_source(__FILE__), trace_glPushMatrix(__VA_ARGS__))
#define glScissor(...) (bench_trace_source(__FILE__), trace_glScissor(__VA_ARGS__))
#define glShadeModel(...) (bench_trace_source(__FILE__), trace_glShadeModel(__VA_ARGS__))
#define glTexCoordPointer(...) (bench_trace_source(__FILE__), trace_glTexCoordPointer(__VA_ARGS__))
#define glTexEnvx(...) (bench_trace_source(__FILE__), trace_glTexEnvx(__VA_ARGS__))
#define glTexImage2D(...) (bench_trace_source(__FILE__), trace_glTexImage2D(__VA_ARGS__))
#define glTexParameterx(...) (bench_trace_source(__FILE__), trace_glTexParameterx(__VA_ARGS__))
#define glVertexPointer(...) (bench_trace_source(__FILE__), trace_glVertexPointer(__VA_ARGS__))
#define glViewport(...) (bench_trace_source(__FILE__), trace_glViewport(__VA_ARGS__))
#define glReadPixels(...) (bench_trace_source(__FILE__), trace_glReadPixels(__VA_ARGS__))
#define glCompressedTexImage2D(...) (bench_trace_source(__FILE__), trace_glCompressedTexImage2D(__VA_ARGS__))
#define glTexParameteri(...) (bench_trace_source(__FILE__), trace_glTexParameteri(__VA_ARGS__))
#define glTexSubImage2D(...) (bench_trace_source(__FILE__), trace_glTexSubImage2D(__VA_ARGS__))
#endif
