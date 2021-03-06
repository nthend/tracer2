#pragma once

#include <opencl.h>

#define HIT_FSIZE (12*sizeof(float))
#define HIT_ISIZE (4*sizeof(int))
#define HIT_FOFFSET 0
#define HIT_IOFFSET HIT_FSIZE
#define HIT_SIZE (HIT_FSIZE + HIT_ISIZE)

#define HIT_TYPE_DIRECT  0x01
#define HIT_TYPE_DIFFUSE 0x02

typedef struct
{
	float3 pos;
	float3 dir;
	float3 norm;
	float3 color;
	int2 origin;
	int object;
	int type;
}
Hit;

Hit hit_load(int offset, global const uchar *hit_data)
{
	Hit hit;
	global const uchar *data = hit_data + offset*HIT_SIZE;
	global const float *fdata = (global const float*)(data + HIT_FOFFSET);
	global const int *idata = (global const int*)(data + HIT_IOFFSET);
	hit.pos = vload3(0,fdata);
	hit.dir = vload3(1,fdata);
	hit.norm = vload3(2,fdata);
	hit.color = vload3(3,fdata);
	hit.origin = vload2(0,idata);
	hit.object = idata[2];
	hit.type = idata[3];
	return hit;
}

void hit_store(Hit *hit, int offset, global uchar *hit_data)
{
	global uchar *data = hit_data + offset*HIT_SIZE;
	global float *fdata = (global float*)(data + HIT_FOFFSET);
	global int *idata = (global int*)(data + HIT_IOFFSET);
	vstore3(hit->pos,0,fdata);
	vstore3(hit->dir,1,fdata);
	vstore3(hit->norm,2,fdata);
	vstore3(hit->color,3,fdata);
	vstore2(hit->origin,0,idata);
	idata[2] = hit->object;
	idata[3] = hit->type;
}
