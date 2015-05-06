/** intersect.cl */

void gen_aabb(float3 *lp, float3 *hp, global const float *v, const int n)
{
	float3 h = vload3(0,v), l = vload3(0,v);
	int i;
	for(i = 1; i < n; ++i)
	{
		h = _max(vload3(i,v),h);
		l = _min(vload3(i,v),l);
	}
	*lp = l;
	*hp = h;
}

int test_aabb(float *tp, const float3 p, const float3 d, const float3 lp, const float3 hp)
{
	float3 rd = (float3)(1,1,1)/d;
	
	float3 lt = (lp - p)*rd;
	float3 ht = (hp - p)*rd;
	
	float tmin = max(max(min(lt.x,ht.x),min(lt.y,ht.y)),min(lt.z,ht.z));
	float tmax = min(min(max(lt.x,ht.x),max(lt.y,ht.y)),max(lt.z,ht.z));
	
	*tp = tmin;
	return tmax >= 0 && tmin <= tmax;
}

kernel void intersect(
	global const float *shapes,
	global const uchar *ray_data, global uchar *hit_data,
	global uchar *hit_info, const uint work_size
)
{
	const int size = get_global_size(0);
	const int pos = get_global_id(0);
	
	if(pos >= work_size)
	{
		return;
	}
	
	Ray ray = ray_load(pos,ray_data);
	
	int hit_obj = 0;
	float3 hit_pos, hit_norm;
	
	const uint2 mat[4] = {{1,1},{1,1},{0,1},{0,0}};
	const float osf[4] = {1,0.05,64,0.1};
	
	hit_obj = 0;
	int i;
	float mt;
	for(i = 0; i < 4; ++i)
	{
		float t;
		float3 n,c;
		float3 lp,hp;
		gen_aabb(&lp,&hp,shapes+3*6*i,6);
		if(test_aabb(&t,ray.pos,ray.dir,lp,hp))
		{
			// float osf = dot(hp - lp,(float3)(1,1,1));
			if(intersect_surface(shapes+3*6*i,osf[i],ray.source==i+1,ray.pos,ray.dir,&t,&c,&n))
			{
				if(hit_obj == 0 || t < mt)
				{
					mt = t;
					hit_pos = ray.pos + ray.dir*t;
					hit_obj = i + 1;
					hit_norm = n;
				}
			}
		}
	}
	
	
	Hit hit;
	hit.pos = hit_pos;
	hit.dir = ray.dir;
	hit.norm = hit_norm;
	hit.color = ray.color;
	hit.origin = ray.origin;
	hit.object = hit_obj;
	
	HitInfo info;
	
	info.size = 0;
	info.offset = info.size;
	
	info.pre_size.x = 0;
	info.pre_size.y = 0;
	if(hit_obj)
	{
		info.pre_size.x = mat[hit_obj-1].x;
		info.pre_size.y = mat[hit_obj-1].y;
	}
	info.pre_offset.x = info.pre_size.x;
	info.pre_offset.y = info.pre_size.y;
	
	hit_info_store(&info,pos,hit_info);
	
	hit_store(&hit,pos,hit_data);
}
