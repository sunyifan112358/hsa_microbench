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
	
	struct timespec start, end;
	uint64_t diff = 0;

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

	/*
	 * Query the name of the agent.
	 */
	char name[64] = { 0 };
	err = hsa_agent_get_info(agent, HSA_AGENT_INFO_NAME, name);
	check(Querying the agent name, err);
	printf("The agent name is %s.\n", name);

	/*
	 * Query the maximum size of the queue.
	 */
	uint32_t queue_size = 0;
	err = hsa_agent_get_info(agent, HSA_AGENT_INFO_QUEUE_MAX_SIZE, &queue_size);
	check(Querying the agent maximum queue size, err);
	printf("The maximum queue size is %u.\n", (unsigned int) queue_size);

	/*
	 * Create a queue using the maximum size.
	 */
	hsa_queue_t* queue; 
	err = hsa_queue_create(agent, queue_size, HSA_QUEUE_TYPE_SINGLE, NULL, NULL, UINT32_MAX, UINT32_MAX, &queue);
	check(Creating the queue, err);

	/*
	 * Load the BRIG binary.
	 */
	hsa_ext_module_t module;
	load_module_from_file("kernel_overhead_serial.brig",&module);

	/*
	 * Create hsa program.
	 */
	hsa_ext_program_t program;
	memset(&program,0,sizeof(hsa_ext_program_t));
	err = hsa_ext_program_create(HSA_MACHINE_MODEL_LARGE, HSA_PROFILE_FULL, HSA_DEFAULT_FLOAT_ROUNDING_MODE_DEFAULT, NULL, &program);
	check(Create the program, err);

	/*
	 * Add the BRIG module to hsa program.
	 */
	err = hsa_ext_program_add_module(program, module);
	check(Adding the brig module to the program, err);

	/*
	 * Determine the agents ISA.
	 */
	hsa_isa_t isa;
	err = hsa_agent_get_info(agent, HSA_AGENT_INFO_ISA, &isa);
	check(Query the agents isa, err);

	/*
	 * Finalize the program and extract the code object.
	 */
	hsa_ext_control_directives_t control_directives;
	memset(&control_directives, 0, sizeof(hsa_ext_control_directives_t));
	hsa_code_object_t code_object;
	err = hsa_ext_program_finalize(program, isa, 0, control_directives, "", HSA_CODE_OBJECT_TYPE_PROGRAM, &code_object);
	check(Finalizing the program, err);

	/*
	 * Destroy the program, it is no longer needed.
	 */
	err=hsa_ext_program_destroy(program);
	check(Destroying the program, err);

	/*
	 * Create the empty executable.
	 */
	hsa_executable_t executable;
	err = hsa_executable_create(HSA_PROFILE_FULL, HSA_EXECUTABLE_STATE_UNFROZEN, "", &executable);
	check(Create the executable, err);

	/*
	 * Load the code object.
	 */
	err = hsa_executable_load_code_object(executable, agent, code_object, "");
	check(Loading the code object, err);

	/*
	 * Freeze the executable; it can now be queried for symbols.
	 */
	err = hsa_executable_freeze(executable, "");
	check(Freeze the executable, err);

	/*
	 * Extract the symbol from the executable.
	 */
	hsa_executable_symbol_t symbol;
	err = hsa_executable_get_symbol(executable, NULL, "&empty_kernel", agent, 0, &symbol);
	check(Extract the symbol from the executable, err);

	/*
	 * Extract dispatch information from the symbol
	 */
	uint64_t kernel_object;
	uint32_t kernarg_segment_size;
	uint32_t group_segment_size;
	uint32_t private_segment_size;
	err = hsa_executable_symbol_get_info(symbol, HSA_EXECUTABLE_SYMBOL_INFO_KERNEL_OBJECT, &kernel_object);
	check(Extracting the symbol from the executable, err);
	err = hsa_executable_symbol_get_info(symbol, HSA_EXECUTABLE_SYMBOL_INFO_KERNEL_KERNARG_SEGMENT_SIZE, &kernarg_segment_size);
	check(Extracting the kernarg segment size from the executable, err);
	err = hsa_executable_symbol_get_info(symbol, HSA_EXECUTABLE_SYMBOL_INFO_KERNEL_GROUP_SEGMENT_SIZE, &group_segment_size);
	check(Extracting the group segment size from the executable, err);
	err = hsa_executable_symbol_get_info(symbol, HSA_EXECUTABLE_SYMBOL_INFO_KERNEL_PRIVATE_SEGMENT_SIZE, &private_segment_size);
	check(Extracting the private segment from the executable, err);

	/*
	 * Create a signal to wait for the dispatch to finish.
	 */ 
	hsa_signal_t signal[NUM_KERNEL];
	for (int i = 0; i < NUM_KERNEL; i++)
	{
		err=hsa_signal_create(1, 0, NULL, &(signal[i]));
		check(Creating a HSA signal, err);
	}

	// Create a packet template
	hsa_kernel_dispatch_packet_t packet_template;
	memset(&packet_template, 0, sizeof(hsa_kernel_dispatch_packet_t));
	//packet_template.header |= HSA_FENCE_SCOPE_SYSTEM << HSA_PACKET_HEADER_ACQUIRE_FENCE_SCOPE;
	//packet_template.header |= HSA_FENCE_SCOPE_SYSTEM << HSA_PACKET_HEADER_RELEASE_FENCE_SCOPE;
	packet_template.setup  |= 1 << HSA_KERNEL_DISPATCH_PACKET_SETUP_DIMENSIONS;
	packet_template.workgroup_size_x = (uint16_t)1;
	packet_template.workgroup_size_y = (uint16_t)1;
	packet_template.workgroup_size_z = (uint16_t)1;
	packet_template.grid_size_x = 1;
	packet_template.grid_size_y = 1;
	packet_template.grid_size_z = 1;
	packet_template.completion_signal.handle = 0;
	packet_template.kernel_object = kernel_object;
	packet_template.kernarg_address = NULL;
	packet_template.private_segment_size = private_segment_size;
	packet_template.group_segment_size = group_segment_size;



	clock_gettime(CLOCK_MONOTONIC, &start);
	for (int i = 0; i < NUM_KERNEL; i++)
	{

		/*
		 * Obtain the current queue write index.
		 */
		uint64_t index = hsa_queue_load_write_index_relaxed(queue);

		/*
		 * Write the aql packet at the calculated queue index address.
		 */
		const uint32_t queueMask = queue->size - 1;
		hsa_kernel_dispatch_packet_t* dispatch_packet = &(((hsa_kernel_dispatch_packet_t*)(queue->base_address))[index&queueMask]);

		// Copy packet template
		memcpy(dispatch_packet, &packet_template, 
				sizeof(hsa_kernel_dispatch_packet_t));
		dispatch_packet->completion_signal = signal[i];

		// Dispatch packet
		__atomic_store_n((uint8_t*)(&dispatch_packet->header), 
				(uint8_t)HSA_PACKET_TYPE_KERNEL_DISPATCH, 
				__ATOMIC_RELEASE);

		/*
		 * Increment the write index and ring the doorbell to dispatch the kernel.
		 */
		hsa_queue_store_write_index_relaxed(queue, index+1);
		hsa_signal_store_relaxed(queue->doorbell_signal, index);
		check(Dispatching the kernel, err);

		/*
		 * Wait on the dispatch completion signal until the kernel is finished.
		 */
		hsa_signal_value_t value = hsa_signal_wait_acquire(signal[i],
				HSA_SIGNAL_CONDITION_LT, 1, UINT64_MAX, 
				HSA_WAIT_STATE_BLOCKED);

	}

	clock_gettime(CLOCK_MONOTONIC, &end);
	diff = 1e9 * (end.tv_sec - start.tv_sec) + end.tv_nsec - start.tv_nsec;
	printf("\n\tTest done, time: %f ms\n", (float)diff / 1e6);
	
	/*
	 * Cleanup all allocated resources.
	 */
	for (int i = 0; i < NUM_KERNEL; i++)
	{
		err=hsa_signal_destroy(signal[i]);
		check(Destroying the signal, err);
	}

	err=hsa_executable_destroy(executable);
	check(Destroying the executable, err);

	err=hsa_code_object_destroy(code_object);
	check(Destroying the code object, err);

	err=hsa_queue_destroy(queue);
	check(Destroying the queue, err);

	err=hsa_shut_down();
	check(Shutting down the runtime, err);

	return 0;
}
