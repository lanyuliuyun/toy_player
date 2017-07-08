
#ifndef RENDER_H
#define RENDER_H

#include <SDL2/SDL.h>
#include <process.h>
#include <Windows.h>

#include <list>
using std::list;

class Image;
struct SwsContext;

class Render
{
  public:
    Render(void);
    ~Render();

    int start(void);
    void stop(void);

    void run(void);

    void submit(Image* image);

    void render_routine(void);

  private:
    int init(void);

    void render(Image* image);

    static unsigned __stdcall thread_entry(void* arg);

  private:
    SDL_Window *win_;
    SDL_Surface *surface_;
    struct SwsContext* scaler_;

    list<Image*> images_to_render_;
    CRITICAL_SECTION images_lock_;
    CONDITION_VARIABLE images_wait_;
    uintptr_t thread_;
    bool run_;
};

#endif // !RENDER_H