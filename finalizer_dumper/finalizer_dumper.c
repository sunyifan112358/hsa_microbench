#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "hsa.h"
#include "hsa_ext_finalize.h"

#define NUM_KERNEL 100000

#define check(msg, status) \
	if (status != HSA_STATUS_SUCCESS) { \
		printf("%s failed.\n", #msg); \
		exit(1); \
	} else { \
	}

/*
 * Loads a BRIG module from a specified file. This
 * function does not validate the module.
 */
int load_module_from_file(const char* file_name, hsa_ext_module_t* module) {
	int rc = -1;

	FILE *fp = fopen(file_name, "rb");

	rc = fseek(fp, 0, SEEK_END);

	size_t file_size = (size_t) (ftell(fp) * sizeof(char));

	rc = fseek(fp, 0, SEEK_SET);

	char* buf = (char*) malloc(file_size);

	memset(buf,0,file_size);

	size_t read_size = fread(buf,sizeof(char),file_size,fp);

	if(read_size != file_size) {
		free(buf);
	} else {
		rc = 0;
		*module = (hsa_ext_module_t) buf;
	}

	fclose(fp);

	return rc;
}

/*
 * Determines if the given agent is of type HSA_DEVICE_TYPE_GPU
 * and sets the value of data to the agent handle if it is.
 */
static hsa_status_t get_gpu_agent(hsa_agent_t agent, void *data) {
	hsa_status_t status;
	hsa_device_type_t device_type;
	status = hsa_agent_get_info(agent, HSA_AGENT_INFO_DEVICE, &device_type);
	if (HSA_STATUS_SUCCESS == status && HSA_DEVICE_TYPE_GPU == device_type) {
		hsa_agent_t* ret = (hsa_agent_t*)data;
		*ret = agent;
		return HSA_STATUS_INFO_BREAK;
	}
	return HSA_STATUS_SUCCESS;
}

/*
 * Determines if a memory region can be used for kernarg
 * allocations.
 */
static hsa_status_t get_kernarg_memory_region(hsa_region_t region, void* data) {
	hsa_region_segment_t segment;
	hsa_region_get_info(region, HSA_REGION_INFO_SEGMENT, &segment);
	if (HSA_REGION_SEGMENT_GLOBAL != segment) {
		return HSA_STATUS_SUCCESS;
	}

	hsa_region_global_flag_t flags;
	hsa_region_get_info(region, HSA_REGION_INFO_GLOBAL_FLAGS, &flags);
	if (flags & HSA_REGION_GLOBAL_FLAG_KERNARG) {
		hsa_region_t* ret = (hsa_region_t*) data;
		*ret = region;
		return HSA_STATUS_INFO_BREAK;
	}

	return HSA_STATUS_SUCCESS;
}

int main(int argc, char **argv) {
	hsa_status_t err;

	// Init
	err = hsa_init();
	check(Initializing the hsa runtime, err);

	/* 
	 * Iterate over the agents and pick the gpu agent using 
	 * the get_gpu_agent callback.
	 */
	hsa_agent_t agent;
	err = hsa_iterate_agents(get_gpu_agent, &agent);
	if(err == HSA_STATUS_INFO_BREAK) { err = HSA_STATUS_SUCCESS; }
	check(Getting a gpu agent, err);

	// Query the name of the agent.
	char name[64] = { 0 };
	err = hsa_agent_get_info(agent, HSA_AGENT_INFO_NAME, name);
	check(Querying the agent name, err);
	printf("The agent name is %s.\n", name);
	// Load the BRIG binary.
	hsa_ext_module_t module;
	load_module_from_file(argv[1] ,&module);


	// Create hsa program.
	hsa_ext_program_t program;
	memset(&program, 0, sizeof(hsa_ext_program_t));
	err = hsa_ext_program_create(HSA_MACHINE_MODEL_LARGE, HSA_PROFILE_FULL, HSA_DEFAULT_FLOAT_ROUNDING_MODE_DEFAULT, NULL, &program);
	check(Create the program, err);

	// Add the BRIG module to hsa program.
	err = hsa_ext_program_add_module(program, module);
	check(Adding the brig module to the program, err);

	// Determine the agents ISA.
	hsa_isa_t isa;
	err = hsa_agent_get_info(agent, HSA_AGENT_INFO_ISA, &isa);
	check(Query the agents isa, err);

	// Finalize the program and extract the code object.
	hsa_ext_control_directives_t control_directives;
	memset(&control_directives, 0, sizeof(hsa_ext_control_directives_t));
	hsa_code_object_t code_object;
	err = hsa_ext_program_finalize(program, isa, 0, control_directives, "", HSA_CODE_OBJECT_TYPE_PROGRAM, &code_object);
	check(Finalizing the program, err);

	// Destroy the program, it is no longer needed.
	err=hsa_ext_program_destroy(program);
	check(Destroying the program, err);

	// Cleanup all allocated resources.
	check(Destroying the signal, err);

	err=hsa_code_object_destroy(code_object);
	check(Destroying the code object, err);

	err=hsa_shut_down();
	check(Shutting down the runtime, err);

	return 0;
}
