#include "ray.h"

#include <CL/cl.h>
#ifdef RAY_GL
#include <GL/glew.h>
#include <CL/cl_gl.h>
#ifdef __gnu_linux__
#include <GL/glx.h>
#endif
#endif

#include <math.h>
#include <stdlib.h>
#include <stdio.h>

#include <la/vec.hpp>

#include <cl/session.hpp>
#include <cl/program.hpp>
#include <cl/buffer_object.hpp>
#include <cl/kernel.hpp>
#include <cl/work_range.hpp>
#include <cl/map.hpp>
#include "camera.hpp"
#include <cl/gl_image_object.hpp>

#define __HOST__

#include "opencl.h"

#include <opencl/def/ray.h>
#include <opencl/def/hit.h>
#include <opencl/def/hit_info.h>
#include <opencl/def/camera.h>
#include <opencl/def/index.h>

#define SHAPE_SIZE (6*3*sizeof(float))

#define INDEX_BUFFER_SIZE 4*INDEX_SIZE
#define SHAPE_BUFFER_SIZE 4*SHAPE_SIZE
#define RAYS_PER_PIXEL 4
#define MAX_DIFFUSE_RAYS 2

static unsigned int width, height;

static unsigned long samples = 0;

size_t screen_size;
size_t buffer_size;

cl::session *session = nullptr;
cl::gl_image_object *image = nullptr;
cl::queue *queue = nullptr;
cl::program *program = nullptr;

typedef cl::map<cl::buffer_object*> buffer_map;
typedef cl::map<cl::kernel*> kernel_map;

buffer_map buffers;
cl::map<cl::kernel*> *kernels;

static unsigned ceil_pow2_exp(unsigned num)
{
	int i;
	for(i = 0; (num-1)>>i > 0; ++i) {}
	return i;
}

int rayInit(int w, int h)
{
	int i;
	
	width = w;
	height = h;
	
	session = new cl::session();
	image = new cl::gl_image_object(session->get_context(),width,height);
	queue = &session->get_queue();
	image->bind_queue(queue->get_cl_command_queue());
	
	cl_context context = session->get_context().get_cl_context();
	
	screen_size = width*height;
	buffer_size = 1 << ceil_pow2_exp(screen_size*RAYS_PER_PIXEL);
	
	buffers.insert("ray_data",     new cl::buffer_object(context, RAY_SIZE*buffer_size));
	buffers.insert("hit_data",     new cl::buffer_object(context, HIT_SIZE*buffer_size));
	buffers.insert("cam_fdata",    new cl::buffer_object(context, CAM_SIZE));
	buffers.insert("color_buffer", new cl::buffer_object(context, sizeof(int)*3*screen_size));
	buffers.insert("accum_buffer", new cl::buffer_object(context, sizeof(float)*3*screen_size));
	buffers.insert("hit_info",     new cl::buffer_object(context, HIT_INFO_SIZE*buffer_size));
	buffers.insert("scan_buf",     new cl::buffer_object(context, 2*sizeof(int)*buffer_size));
	buffers.insert("ray_count",    new cl::buffer_object(context, 3*sizeof(int)));
	buffers.insert("random",       new cl::buffer_object(context, sizeof(int)*buffer_size));
	buffers.insert("index",        new cl::buffer_object(context, INDEX_BUFFER_SIZE));
	buffers.insert("shapes",       new cl::buffer_object(context, SHAPE_BUFFER_SIZE));
	buffers.insert("emitters",     new cl::buffer_object(context, SHAPE_BUFFER_SIZE));
	
	for(cl::buffer_object *bo : buffers)
		bo->bind_queue(queue->get_cl_command_queue());
	
	unsigned seed = 0;
	unsigned *random_buffer = (unsigned*)malloc(sizeof(unsigned)*buffer_size);
	for(i = 0; i < int(buffer_size); ++i)
	{
		random_buffer[i] = (seed = 3942082377*seed + 1234567);
	}
	
	buffers["random"]->store_data(random_buffer,buffer_size*sizeof(unsigned));
	queue->flush();
	free(random_buffer);
	
	float *accum_buffer_data = (float*)malloc(3*screen_size*sizeof(float));
	for(i = 0; i < int(3*screen_size); ++i)
	{
		accum_buffer_data[i] = 0.0f;
	}
	buffers["accum_buffer"]->store_data(accum_buffer_data,3*screen_size*sizeof(float));
	queue->flush();
	free(accum_buffer_data);
	
	// Create a program from the kernel source
	program = new cl::program(session->get_context().get_cl_context(),session->get_device_id(),"kernel.c","opencl");
	kernels = &program->get_kernel_map();
	
	for(cl::kernel *k : (*kernels))
	{
		k->bind_queue(session->get_queue());
	}
	
	return 0;
}

void rayDispose()
{
	// cl_uint err;
	session->get_queue().flush();
	
	delete program;
	
	for(cl::buffer_object *bo : buffers)
	{
		delete bo;
	}
	
	delete image;
	delete session;
}

void rayLoadGeometry(const float *geom, size_t size)
{
	uchar index_data[4*INDEX_SIZE];
	for(int j = 0; j < 4; ++j) {
		Index idx;
		idx.id = j + 1;
		idx.ptr = j*3*6;
		
		idx.type = OBJECT_TYPE_SURFACE;
		if(j == 2)
			idx.type = OBJECT_TYPE_SPHERE;
		
		fvec3 center = nullfvec3;
		int i;
		for(i = 0; i < 6; ++i) {
			vec3 p;
			p.memcopy(geom + j*6*3 + i*3);
			center += p;
		}
		center /= 6.0f;
		idx.pos.x = center.x();
		idx.pos.y = center.y();
		idx.pos.z = center.z();
		
		float rad2 = 0.0f;
		for(i = 0; i < 6; ++i) {
			vec3 p;
			p.memcopy(geom + j*6*3 + i*3);
			fvec3 vd = p - center;
			float dist2 = vd*vd;
			if(dist2 > rad2) {
				rad2 = dist2;
			}
		}
		idx.rad = sqrt(rad2);
		index_store(&idx, j, index_data);
	}
	buffers["index"]->store_data(index_data, 4*INDEX_SIZE);
	buffers["shapes"]->store_data(geom, size);
	queue->flush();
}

void rayLoadEmitters(const float *geom, size_t size) 
{
	buffers["emitters"]->store_data(geom, size);
	queue->flush();
}

void rayClear()
{
	samples = 0;
}

void rayUpdateMotion()
{
	__move_cam_pos();
	__move_cam_ori();
	rayClear();
}

int rayRender()
{
	
	// Create buffers
	float cam_array[CAM_SIZE/sizeof(float)] =
	{
	  cam_pos[0], cam_pos[1], cam_pos[2],
	  cam_ori[0], cam_ori[1], cam_ori[2],
	  cam_ori[3], cam_ori[4], cam_ori[5],
	  cam_ori[6], cam_ori[7], cam_ori[8],
	  cam_pre_pos[0], cam_pre_pos[1], cam_pre_pos[2],
	  cam_pre_ori[0], cam_pre_ori[1], cam_pre_ori[2],
	  cam_pre_ori[3], cam_pre_ori[4], cam_pre_ori[5],
	  cam_pre_ori[6], cam_pre_ori[7], cam_pre_ori[8],
	  cam_fov, cam_rad, cam_dof
	};
	buffers["cam_fdata"]->store_data(cam_array);
	
	cl::work_range range2d = {width,height};
	size_t work_size;
	//size_t global_work_size[2] = {width,height};
	//size_t local_work_size[2] = {8,8};
	
	// start
	(*kernels)["start"]->evaluate(
	  range2d,
	  buffers["ray_data"],
	  buffers["cam_fdata"],
	  buffers["random"]
	)
#ifdef PRINT_TIME
	->print_time()
#endif // PRINT_TIME
	;

	queue->flush();
	
	unsigned int ray_count = width*height;
	unsigned int rc2[2] = {ray_count,0};
	
	uint sdr = 0;//(1 - samples)*(1 - (int)samples > 0);
	int depth = 3 - sdr;
	for(int i = 0; i < depth; ++i)
	{
		cl::work_range range1d = {ray_count};
		
		work_size = ray_count;
		
		// intersect
		(*kernels)["intersect"]->evaluate(
		  range1d,
		  buffers["shapes"],
		  buffers["index"],
		  buffers["ray_data"],
		  buffers["hit_data"],
		  buffers["hit_info"],
		  int(work_size)
		)
#ifdef PRINT_TIME
		->print_time()
#endif // PRINT_TIME
		;
		queue->flush();
		
		if(i < depth - 1)
		{
			int dev;
			
			cl::buffer_object *scan_buf = buffers["scan_buf"];
			
			dev = 0;
			int pow2_exp = ceil_pow2_exp(ray_count);
			unsigned int work_size_pow2 = 1 << pow2_exp;
			
			cl::work_range range1d_pow2 = {work_size_pow2};
			
			(*kernels)["prepare"]->evaluate(
				range1d_pow2,
				buffers["hit_info"],
				scan_buf,
				int(work_size),
				int(work_size_pow2)
			)
#ifdef PRINT_TIME
			->print_time()
#endif // PRINT_TIME
			;
			
			queue->flush();
			
			// sweep up
			for(; dev < pow2_exp; ++dev)
			{
				range1d_pow2 = {1u << (pow2_exp - dev - 1)};
				
				(*kernels)["sweep_up"]->evaluate(
					range1d_pow2,
					scan_buf,
					buffers["ray_count"],
					int(dev),
					int(work_size_pow2)
				)
#ifdef PRINT_TIME
				->print_time()
#endif // PRINT_TIME
				;
				queue->flush();
			}
			
			--dev;
			
			// sweep down
			for(; dev >= 0; --dev)
			{
				range1d_pow2 = {1u << (pow2_exp - dev - 1)};
				
				(*kernels)["sweep_down"]->evaluate(
					range1d_pow2,
					scan_buf,
					int(dev),
					int(work_size_pow2)
				)
#ifdef PRINT_TIME
				->print_time()
#endif // PRINT_TIME
				;
				queue->flush();
			}
			
			buffers["ray_count"]->load_data(&ray_count,sizeof(int));
			buffers["ray_count"]->load_data(rc2,sizeof(int),2*sizeof(int));
			
			unsigned int mfactor = 0;
			if(rc2[1] != 0)
			{
				mfactor = (buffer_size - rc2[0])/rc2[1];
				unsigned mdif = MAX_DIFFUSE_RAYS;
				/*
				unsigned shift = (2 - samples)*(2 - (int)samples > 0);
				switch(i)
				{
				case 0:
					mdif = 4>>shift;
					break;
				case 1:
					mdif = 2>>shift;
					break;
				default:
					mdif = 1>>shift;
					break;
				}
				if(mdif <= 0)
				{
					mdif = 1;
				}
				*/
				if(mfactor > mdif)
				{
					mfactor = mdif;
				}
			}
			ray_count = rc2[0] + mfactor*rc2[1];
			
			(*kernels)["expand"]->evaluate(
				range1d,
				buffers["hit_info"],
				scan_buf,
				int(mfactor),
				int(work_size)
			)
#ifdef PRINT_TIME
			->print_time()
#endif // PRINT_TIME
			;
			
			queue->flush();
		}
		
		// fprintf(stdout,"ray_count: %d, rc2: {%d,%d}\n",ray_count,rc2[0],rc2[1]);
		// return 0;
		
		// produce
		(*kernels)["produce"]->evaluate(
		  range1d,
		  buffers["hit_data"],
		  buffers["ray_data"],
		  buffers["hit_info"],
		  buffers["emitters"],
		  buffers["color_buffer"],
		  int(width),
		  int(work_size),
		  buffers["random"]
		)
#ifdef PRINT_TIME
		->print_time()
#endif // PRINT_TIME
		;
		
		queue->flush();
		
		if(ray_count == 0)
		{
			break;
		}
	}
	
	++samples;
	uint csmp = samples - (samples < 3)*(samples - 1);
	float mul = 1.0/csmp;
	
	(*kernels)["draw"]->evaluate(
	  range2d,
	  buffers["color_buffer"],
	  buffers["accum_buffer"],
	  float(mul),
	  image
	)
#ifdef PRINT_TIME
	->print_time()
#endif // PRINT_TIME
	;
	
	queue->flush();
	
	(*kernels)["clear"]->evaluate(
	  range2d,
	  buffers["color_buffer"]
	)
#ifdef PRINT_TIME
	->print_time()
#endif // PRINT_TIME
	;
	
	return 0;
}

#ifdef CL_GL_INTEROP
GLuint rayGetGLTexture()
{
	return image->get_texture();
}
#endif // CL_GL_INTEROP
