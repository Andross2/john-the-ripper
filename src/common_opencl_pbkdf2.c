/* This software is Copyright (c) 2012 Sayantan Datta <std2048 at gmail dot com>
* and it is hereby released to the general public under the following terms:
* Redistribution and use in source and binary forms, with or without modification, are permitted.
* Based on S3nf implementation http://openwall.info/wiki/john/MSCash2
* This format supports salts upto 19 characters. Origial S3nf implementation supports only upto 8 charcters.
*/

#include "common_opencl_pbkdf2.h"
#include <string.h>
#include <math.h>



	static cl_platform_id pltfrmid[MAX_PLATFORMS];

	static cl_device_id devid[MAX_PLATFORMS][MAX_DEVICES_PER_PLATFORM];

	static cl_context cntxt[MAX_PLATFORMS][MAX_DEVICES_PER_PLATFORM];

	static cl_command_queue cmdq[MAX_PLATFORMS][MAX_DEVICES_PER_PLATFORM];

	static cl_kernel krnl[MAX_PLATFORMS][MAX_DEVICES_PER_PLATFORM];

	static cl_program prg[MAX_PLATFORMS][MAX_DEVICES_PER_PLATFORM];

	static cl_int err;

	static cl_event events[MAX_PLATFORMS*MAX_DEVICES_PER_PLATFORM];

	static size_t lws[MAX_PLATFORMS][MAX_DEVICES_PER_PLATFORM];

	static long double exec_time_inv[MAX_PLATFORMS][MAX_DEVICES_PER_PLATFORM]={{-1.000}};

	static int event_ctr=0;

	static cl_ulong kernelExecTimeNs = CL_ULONG_MAX;

	static char PROFILE = 0 ;

	static gpu_mem_buffer gpu_buffer[MAX_PLATFORMS][MAX_DEVICES_PER_PLATFORM];

        static unsigned int active_dev_ctr=0;

        static int store_platform_no[MAX_PLATFORMS*MAX_DEVICES_PER_PLATFORM]={-1};

        static int store_dev_no[MAX_PLATFORMS*MAX_DEVICES_PER_PLATFORM]={-1};



static gpu_mem_buffer exec_pbkdf2(cl_uint *,cl_uint *,cl_uint ,cl_uint *,cl_uint ,int ,int  )	;



static void clean_gpu_buffer(gpu_mem_buffer *pThis)
{
	HANDLE_CLERROR(clReleaseMemObject(pThis->pass_gpu),"Release Memory Object FAILED.");

	HANDLE_CLERROR(clReleaseMemObject(pThis->hash_out_gpu),"Release Memory Object FAILED.");

	HANDLE_CLERROR(clReleaseMemObject(pThis->salt_gpu),"Release Memory Object FAILED.");
}

void clean_all_buffer()
{	 int i;

	 for(i=0;i<active_dev_ctr;i++)
	    clean_gpu_buffer(&gpu_buffer[store_platform_no[i]][store_dev_no[i]]);

}

static void find_best_workgroup(int pltform_no,int dev_no)
{
        size_t _lws=0;

	cl_device_type dTyp;

	cl_uint *dcc_hash_host=(cl_uint*)malloc(4*sizeof(cl_uint)*64000);

	cl_uint *dcc2_hash_host=(cl_uint*)malloc(4*sizeof(cl_uint)*64000);

	cl_uint salt_api[9],length=10;

	event_ctr=0;

	HANDLE_CLERROR(clGetDeviceInfo(devid[pltform_no][dev_no],CL_DEVICE_TYPE,sizeof(cl_device_type),&dTyp,NULL),"Failed Device Info");

	///Set Dummy DCC hash , unicode salt and ascii salt(username) length

	memset(dcc_hash_host,0xb5,4*sizeof(cl_uint)*64000);

	memset(salt_api,0xfe,9*sizeof(cl_uint));



	cmdq[pltform_no][dev_no] = clCreateCommandQueue(cntxt[pltform_no][dev_no], devid[pltform_no][dev_no], CL_QUEUE_PROFILING_ENABLE,&err);

	HANDLE_CLERROR(err, "Error creating command queue");

	PROFILE=1;

	kernelExecTimeNs = CL_ULONG_MAX;


	///Find best local work size
	while(1){
		_lws=lws[pltform_no][dev_no];

		if(dTyp==CL_DEVICE_TYPE_CPU){

			exec_pbkdf2(dcc_hash_host,salt_api,length,dcc2_hash_host,4000,pltform_no,dev_no );

			exec_time_inv[pltform_no][dev_no]=exec_time_inv[pltform_no][dev_no]/16;
		}
		else
			exec_pbkdf2(dcc_hash_host,salt_api,length,dcc2_hash_host,64000,pltform_no,dev_no );

		if(lws[pltform_no][dev_no]<=_lws) break;
	}

	PROFILE=0;

	printf("Optimal Work Group Size:%d\n",(int)lws[pltform_no][dev_no]);

	printf("Kernel Execution Speed (Higher is better):%Lf\n",exec_time_inv[pltform_no][dev_no]);

	free(dcc_hash_host);

	free(dcc2_hash_host);


}

size_t 	select_device(int platform_no,int dev_no)
{
      	lws[platform_no][dev_no]= 16;

	opencl_init("$JOHN/pbkdf2_kernel.cl", dev_no, platform_no);

	pltfrmid[platform_no]=platform[platform_no];

	devid[platform_no][dev_no]=devices[dev_no];

	cntxt[platform_no][dev_no]=context[dev_no];

	prg[platform_no][dev_no]=program[dev_no];

	krnl[platform_no][dev_no]=clCreateKernel(prg[platform_no][dev_no],"PBKDF2",&err) ;

	if(err) {printf("Create Kernel PBKDF2 FAILED\n"); return 0;}

	gpu_buffer[platform_no][dev_no].pass_gpu=clCreateBuffer(cntxt[platform_no][dev_no],CL_MEM_READ_ONLY,4*MAX_KEYS_PER_CRYPT*sizeof(cl_uint),NULL,&err);
		if((gpu_buffer[platform_no][dev_no].pass_gpu==(cl_mem)0)) { HANDLE_CLERROR(err, "Create Buffer FAILED"); }

	gpu_buffer[platform_no][dev_no].salt_gpu=clCreateBuffer(cntxt[platform_no][dev_no],CL_MEM_READ_ONLY,(MAX_SALT_LENGTH/2 +1)*sizeof(cl_uint),NULL,&err);
	        if((gpu_buffer[platform_no][dev_no].salt_gpu==(cl_mem)0)) { HANDLE_CLERROR(err, "Create Buffer FAILED"); }

	gpu_buffer[platform_no][dev_no].hash_out_gpu=clCreateBuffer(cntxt[platform_no][dev_no],CL_MEM_WRITE_ONLY,4*MAX_KEYS_PER_CRYPT*sizeof(cl_uint),NULL,&err);
	        if((gpu_buffer[platform_no][dev_no].hash_out_gpu==(cl_mem)0)) {HANDLE_CLERROR(err, "Create Buffer FAILED"); }

	HANDLE_CLERROR(clSetKernelArg(krnl[platform_no][dev_no],0,sizeof(cl_mem),&gpu_buffer[platform_no][dev_no].pass_gpu),"Set Kernel Arg FAILED arg0");

	HANDLE_CLERROR(clSetKernelArg(krnl[platform_no][dev_no],1,sizeof(cl_mem),&gpu_buffer[platform_no][dev_no].salt_gpu),"Set Kernel Arg FAILED arg1");

	HANDLE_CLERROR(clSetKernelArg(krnl[platform_no][dev_no],4,sizeof(cl_mem),&gpu_buffer[platform_no][dev_no].hash_out_gpu),"Set Kernel Arg FAILED arg4");

	find_best_workgroup(platform_no,dev_no);

	cmdq[platform_no][dev_no]=queue[dev_no];

	store_platform_no[active_dev_ctr]=platform_no;

	store_dev_no[active_dev_ctr++]=dev_no;

	return lws[platform_no][dev_no];
}

size_t select_default_device()
{
	  return select_device(0,0);
}


static size_t max_lws()
{
	int i;

	size_t max=0;

	for(i=0;i<active_dev_ctr;++i)
		if(max<lws[store_platform_no[i]][store_dev_no[i]])
			max=lws[store_platform_no[i]][store_dev_no[i]];

	return max;
}

void pbkdf2_divide_work(cl_uint *pass_api,cl_uint *salt_api,cl_uint saltlen_api,cl_uint *hash_out_api,cl_uint num)
{
	  double total_exec_time_inv=0;

	  int i;

	  unsigned int work_part,work_offset=0,lws_max=max_lws();

	  cl_int ret;

	  event_ctr=0;

	  memset(hash_out_api,0,num*sizeof(cl_uint));

	  /// Make num multiple of lws_max
	  if(num%lws_max!=0)
		num=(num/lws_max + 1)*lws_max;

	  ///Divide work only if number of keys is greater than 8192, else use first device selected
	  if(num>8192){

		///Calculates t0tal Kernel Execution Speed
		for(i=0;i<active_dev_ctr;++i){
			total_exec_time_inv+=exec_time_inv[store_platform_no[i]][store_dev_no[i]];
		}

		///Calculate work division ratio
		for(i=0;i<active_dev_ctr;++i)
			exec_time_inv[store_platform_no[i]][store_dev_no[i]]/=total_exec_time_inv;

		///Divide memory and work
		for(i=0;i<active_dev_ctr;++i){
			if(i==active_dev_ctr-1){
				work_part= num- work_offset;
				if(work_part%lws_max!=0)
				work_part=(work_part/lws_max + 1)*lws_max;
			}
			else{
				work_part=num*exec_time_inv[store_platform_no[i]][store_dev_no[i]];
				if(work_part%lws_max!=0)
					work_part=(work_part/lws_max + 1)*lws_max;
			}

		///call to exec_pbkdf2()
#ifdef _DEBUG
		printf("Work Offset:%d  Work Part Size:%d %d\n",work_offset,work_part,event_ctr);
#endif
		exec_pbkdf2(pass_api+4*work_offset,salt_api,saltlen_api,hash_out_api+4*work_offset,work_part,store_platform_no[i],store_dev_no[i]);

		work_offset+=work_part;


		}

		///Synchronize Device memory and Host memory
		for(i=active_dev_ctr-1;i>=0;--i)
			HANDLE_CLERROR(clFlush(cmdq[store_platform_no[i]][store_dev_no[i]]),"Flush Error");


		for(i=0;i<active_dev_ctr;++i){
			while(1){
				HANDLE_CLERROR(clGetEventInfo(events[i],CL_EVENT_COMMAND_EXECUTION_STATUS,sizeof(cl_int),&ret,NULL),"Error in Get Event Info");
				if((ret)==CL_COMPLETE) break;
#ifdef  _DEBUG
				 printf("%d%d ", ret,i);
#endif
			}
		}

		for(i=0;i<active_dev_ctr;++i)
			HANDLE_CLERROR(clFinish(cmdq[store_platform_no[i]][store_dev_no[i]]),"Finish Error");

	 }

	 else{
		exec_pbkdf2(pass_api,salt_api,saltlen_api,hash_out_api,num, store_platform_no[0],store_dev_no[0]);
		HANDLE_CLERROR(clFinish(cmdq[store_platform_no[0]][store_dev_no[0]]),"Finish Error");

	}

}


static gpu_mem_buffer exec_pbkdf2(cl_uint *pass_api,cl_uint *salt_api,cl_uint saltlen_api,cl_uint *hash_out_api,cl_uint num,int platform_no,int dev_no )
{
	cl_event evnt;

	size_t N=num,M=lws[platform_no][dev_no];

	HANDLE_CLERROR(clEnqueueWriteBuffer(cmdq[platform_no][dev_no],gpu_buffer[platform_no][dev_no].pass_gpu,CL_TRUE,0,4*num*sizeof(cl_uint),pass_api,0,NULL,NULL ), "Copy data to gpu");

	HANDLE_CLERROR(clEnqueueWriteBuffer(cmdq[platform_no][dev_no],gpu_buffer[platform_no][dev_no].salt_gpu,CL_TRUE,0,(MAX_SALT_LENGTH/2 + 1)*sizeof(cl_uint),salt_api,0,NULL,NULL ), "Copy data to gpu");

	HANDLE_CLERROR(clSetKernelArg(krnl[platform_no][dev_no],2,sizeof(cl_uint),&saltlen_api),"Set Kernel Arg FAILED arg2");

	HANDLE_CLERROR(clSetKernelArg(krnl[platform_no][dev_no],3,sizeof(cl_uint),&num),"Set Kernel Arg FAILED arg3");

	err=clEnqueueNDRangeKernel(cmdq[platform_no][dev_no],krnl[platform_no][dev_no],1,NULL,&N,&M,0,NULL,&evnt);

    	if(err){
		if(PROFILE){
			lws[platform_no][dev_no]=lws[platform_no][dev_no]/2;
	  	}

		else
			HANDLE_CLERROR(err,"Enque Kernel Failed");

		return gpu_buffer[platform_no][dev_no];
	}



        if(PROFILE){

		cl_ulong startTime, endTime;

		HANDLE_CLERROR(CL_SUCCESS!=clWaitForEvents(1,&evnt),"SYNC FAILED");

		HANDLE_CLERROR(clFinish(cmdq[platform_no][dev_no]), "clFinish error");

		clGetEventProfilingInfo(evnt, CL_PROFILING_COMMAND_SUBMIT, sizeof(cl_ulong), &startTime, NULL);

		clGetEventProfilingInfo(evnt, CL_PROFILING_COMMAND_END,    sizeof(cl_ulong), &endTime, NULL);

		if ((endTime - startTime) < kernelExecTimeNs) {

			kernelExecTimeNs = endTime - startTime;

			//printf("%d\n",(int)kernelExecTimeNs);

			lws[platform_no][dev_no]  =lws[platform_no][dev_no]*2;

			exec_time_inv[platform_no][dev_no]=  (long double)pow(10,9)/(long double)kernelExecTimeNs;

	        }

         }

         else{
		HANDLE_CLERROR(clEnqueueReadBuffer(cmdq[platform_no][dev_no],gpu_buffer[platform_no][dev_no].hash_out_gpu,CL_FALSE,0,4*num*sizeof(cl_uint),hash_out_api, 1, &evnt, &events[event_ctr++]),"Write FAILED");
	 }

	 return gpu_buffer[platform_no][dev_no];
}
