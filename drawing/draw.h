#pragma once
#include <dlfcn.h>
#include <stdio.h>
#include <GL/gl.h>
#include <GL/glx.h>


namespace GL
{
  void setupOrtho(void);
  void restoreGL(void);

  void drawRect(float x, float y, float width, float height, int hp);
}

typedef void (*glXSwapBuffersPtr)(Display *, GLXDrawable);
void glXSwapBuffers(Display *dpy, GLXDrawable drawable);
