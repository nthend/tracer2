#include <CL/cl.h>
#include <CL/cl_gl.h>

#include <SDL2/SDL.h>
#include <GL/glew.h>

#ifdef __gnu_linux__
#include <GL/glx.h>
#endif

#include <math.h>
#include <stdlib.h>
#include <stdio.h>

#include "gl.h"
#include "ray.h"

//#define RECORD
//#define PLAYBACK
//#define SAVE_IMAGE
#define PRINT_FPS

//#define FIXED_SAMPLE_RATE

#ifdef FIXED_SAMPLE_RATE
#define SAMPLES 0x8
#else // FIXED_SAMPLE_RATE
#define FRAME_PERIOD 18 //ms
#endif // FIXED_SAMPLE_RATE

#define MOTION

static int width = 800;//1280;
static int height = 600;//720;
static SDL_Window *sdl_window = NULL;
static SDL_GLContext sdl_glcontext = NULL;

static float yaw = 0.0f, pitch = 0.0f;
static float pos[3] = {0.0f,0.0f,0.0f};
static float fov = 0.5f, rad = 0.01f, dof = 4.0f;

int main(int argc, char *argv[])
{
	sdl_window = SDL_CreateWindow(
			"OpenCL",
			SDL_WINDOWPOS_CENTERED,
			SDL_WINDOWPOS_CENTERED,
			width, height,
			SDL_WINDOW_OPENGL// | SDL_WINDOW_RESIZABLE
		);
	
	if(sdl_window == NULL)
	{
		fprintf(stderr,"Could not create SDL_Window\n");
		return -1;
	}
	
	sdl_glcontext = SDL_GL_CreateContext(sdl_window);
	
	if(sdl_glcontext == NULL)
	{
		fprintf(stderr,"Could not create SDL_GL_Context\n");
		return -2;
	}

	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER,1);
	SDL_GL_SetAttribute(SDL_GL_RED_SIZE,5);
	SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE,6);
	SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE,5);
	SDL_GL_SetSwapInterval(1);

	GLenum glew_status = glewInit();
	if(GLEW_OK != glew_status)
	{
		fprintf(stderr,"Could not init glew: %s\n",glewGetErrorString(glew_status));
		return -3;
	}
	if(!GLEW_VERSION_2_0)
	{
		fprintf(stderr,"No support for OpenGL 2.0 found\n");
		return -4;
	}
	
	initGL();
	if(rayInit(width,height))
	{
		disposeGL();
		return -1;
	}
	
	double t = 0.0;
	double sa[2] = {0.5, 1.0}, sd = -1.0;
	double sf[6] = {-0.53, -0.47, -0.65, 0.58, 0.61, 0.55};
	
	float shape_coord[3*6*3] = {
	  0.0,-1.0,sd, sqrt(3.0)/2.0,0.5,sd, -sqrt(3.0)/2.0,0.5,sd,
	  sqrt(3.0)/2.0,-0.5, sd,0.0,1.0,sd, -sqrt(3.0)/2.0,-0.5,sd,
	  0,-4,-3, 2*sqrt(3.0),2,-3, -2*sqrt(3.0),2,-3,
	  2*sqrt(3.0),-2,-1, 0,4,-1, -2*sqrt(3.0),-2,-1,
	  0.0,-0.5,1.5, sqrt(3.0)/4.0,0.25,1.5, -sqrt(3.0)/4.0,0.25,1.5,
	  sqrt(3.0)/4.0,-0.25,1.0, 0.0,0.5,1.0, -sqrt(3.0)/4.0,-0.25,1.0
	};
	rayLoadGeometry(shape_coord,3*6*3*sizeof(float));
	rayLoadEmitters(shape_coord + 2*6*3, 6*3*sizeof(float));
	
#ifdef RECORD
	FILE *rec_file = fopen("record","wb");
#endif
	
#ifdef PLAYBACK
	FILE *rec_file = fopen("record","rb");
#endif
	
	int done = 0;
	SDL_Event event;
	int ws = 0, as = 0, ss = 0, ds = 0, spcs = 0, ctls = 0;
	int mmode = 1;
	int tick = SDL_GetTicks(), frame = 0, pass = 0;
	
	SDL_Surface *image = SDL_CreateRGBSurface(SDL_SWSURFACE,width,height,24,0x0000ff,0x00ff00,0xff0000,0x000000);
	int counter = 0;
	
	raySetPos(pos);
	raySetFov(fov);
	raySetDof(rad,dof);
	raySetOri(yaw,pitch);
	rayUpdateMotion();
	if(rayRender() != 0)
	{
		return 1;
	}

#ifndef PLAYBACK
	SDL_SetRelativeMouseMode((SDL_bool)mmode);
#endif
	while(!done)
	{
		int updv = 0;
		int updm = 0;
		while(SDL_PollEvent(&event))
		{
			if(event.type == SDL_QUIT)
			{
				done = 1;
			}
			else
			if(event.type == SDL_KEYDOWN)
			{
				switch(event.key.keysym.sym)
				{
				case SDLK_w:
					ws = 1;
					break;
				case SDLK_a:
					as = 1;
					break;
				case SDLK_s:
					ss = 1;
					break;
				case SDLK_d:
					ds = 1;
					break;
				case SDLK_SPACE:
					spcs = 1;
					break;
				case SDLK_LCTRL:
					ctls = 1;
					break;
				}
			}
			else
			if(event.type == SDL_KEYUP)
			{
				switch(event.key.keysym.sym)
				{
				case SDLK_w:
					ws = 0;
					break;
				case SDLK_a:
					as = 0;
					break;
				case SDLK_s:
					ss = 0;
					break;
				case SDLK_d:
					ds = 0;
					break;
				case SDLK_SPACE:
					spcs = 0;
					break;
				case SDLK_LCTRL:
					ctls = 0;
					break;
				case SDLK_ESCAPE:
					/*
					mmode = !mmode;
					SDL_SetRelativeMouseMode(mmode);
					break;
					*/
					done = 1;
				}
			}
			else
			if(event.type == SDL_MOUSEMOTION && mmode)
			{
				float mspd = 0.002;
				yaw -= mspd*event.motion.xrel;
				pitch -= mspd*event.motion.yrel;
				if(yaw < 0.0f)
				{
					yaw += 2.0f*M_PI;
				}
				else
				if(yaw > 2*M_PI)
				{
					yaw -= 2*M_PI;
				}
				if(pitch < -M_PI/2.0f)
				{
					pitch = -M_PI/2.0f;
				}
				else
				if(pitch > M_PI/2.0f)
				{
					pitch = M_PI/2.0f;
				}
				updm = 1;
			}
			else
			if(event.type == SDL_MOUSEWHEEL)
			{
				if(event.wheel.y > 0)
				{
					fov /= 1.2f;
					updv = 1;
				}
				else
				if(event.wheel.y < 0)
				{
					fov *= 1.2f;
					updv = 1;
				}
			}
		}
		
#ifdef RECORD
		int ndone = 1;
		fwrite((void*)&ndone,sizeof(int),1,rec_file);
#endif

#ifdef PLAYBACK
		int ndone = 0;
		fread((void*)&ndone,sizeof(int),1,rec_file);
		if(!ndone)
		{
			break;
		}
#endif

		int upd = 0;
#ifdef MOTION
		upd = 1;
#endif // MOTION
#ifndef PLAYBACK
		if(ws || as || ss || ds || spcs || ctls)
		{
			const float spd = 0.1;
			pos[0] += spd*((ss - ws)*sin(yaw)*cos(pitch) + (ds - as)*cos(yaw));
			pos[1] += spd*((ws - ss)*cos(yaw)*cos(pitch) + (ds - as)*sin(yaw));
			pos[2] += spd*(spcs - ctls) + spd*(ws - ss)*sin(pitch);
			upd = 1;
		}
		if(updv || updm)
		{
			upd = 1;
		}
#endif

#ifdef RECORD
			fwrite((void*)pos,3*sizeof(float),1,rec_file);
			fwrite((void*)&fov,sizeof(float),1,rec_file);
			fwrite((void*)&rad,sizeof(float),1,rec_file);
			fwrite((void*)&dof,sizeof(float),1,rec_file);
			fwrite((const void*)&yaw,sizeof(float),1,rec_file);
			fwrite((const void*)&pitch,sizeof(float),1,rec_file);
			fwrite((void*)&upd,sizeof(int),1,rec_file);
#endif

#ifdef PLAYBACK
			fread((void*)pos,3*sizeof(float),1,rec_file);
			fread((void*)&fov,sizeof(float),1,rec_file);
			fread((void*)&rad,sizeof(float),1,rec_file);
			fread((void*)&dof,sizeof(float),1,rec_file);
			fread((void*)&yaw,sizeof(float),1,rec_file);
			fread((void*)&pitch,sizeof(float),1,rec_file);
			fread((void*)&upd,sizeof(int),1,rec_file);
#endif
		
		if(upd)
		{
			raySetPos(pos);
			raySetFov(fov);
			raySetDof(rad,dof);
			raySetOri(yaw,pitch);
			rayClear();
		}
		
#ifdef FIXED_SAMPLE_RATE
		int i;
		for(i = 0; i < SAMPLES; ++i)
		{
			if(rayRender() != 0)
			{
				return 1;
			}
			++pass;
		}
#else // FIXED_SAMPLE_RATE
		long st = SDL_GetTicks();
		while(SDL_GetTicks() - st < FRAME_PERIOD)
		{
			if(rayRender() != 0)
			{
				return 1;
			}
			++pass;
		}
#endif // FIXED_SAMPLE_RATE
		if(upd)
		{
#ifdef MOTION
			shape_coord[2]  = sa[0]*sin(sf[0]*t) + sd;
			shape_coord[5]  = sa[0]*sin(sf[1]*t) + sd;
			shape_coord[8]  = sa[0]*sin(sf[2]*t) + sd;
			shape_coord[11] = sa[1]*sin(sf[3]*t) + sd;
			shape_coord[14] = sa[1]*sin(sf[4]*t) + sd;
			shape_coord[17] = sa[1]*sin(sf[5]*t) + sd;
			rayLoadGeometry(shape_coord, 6*3*sizeof(float));
#endif
			rayUpdateMotion();
			t += 0.08;
		}
		
		GLuint texture = rayGetGLTexture();
		drawGLTexture(texture);
		
		glFlush();
		
#ifdef SAVE_IMAGE
		char num[21];
		sprintf(num,"video/frame%05d.bmp", counter);
		num[20] = '\0';
		++counter;
		glBindTexture(GL_TEXTURE_2D, texture);
		glReadPixels(0,0,width,height,GL_RGB,GL_UNSIGNED_BYTE,image->pixels);
		SDL_SaveBMP(image,num);
		glBindTexture(GL_TEXTURE_2D, 0);
#endif // SAVE_IMAGE
		
		SDL_GL_SwapWindow(sdl_window);
		
#ifdef PRINT_FPS
		++frame;
		int ntick = SDL_GetTicks();
		if(ntick - tick > 1000)
		{
			tick = ntick;
			fprintf(stdout,"fps: %d, pass: %.1f\n",frame,(float)pass/frame);
			fflush(stdout);
			frame = 0;
			pass = 0;
		}
#endif // PRINT_FPS
	}

#ifdef RECORD
	int ndone = 0;
	fwrite((void*)&ndone,sizeof(int),1,rec_file);
#endif
	
#ifdef RECORD
	fclose(rec_file);
#endif
	
#ifdef PLAYBACK
	fclose(rec_file);
#endif
	
	// Clean up
	rayDispose();
	disposeGL();
	
	SDL_FreeSurface(image);
	
	SDL_GL_DeleteContext(sdl_glcontext);
	SDL_DestroyWindow(sdl_window);
	return 0;
}
