//
// Created by Russell Forrest on 01/11/2023.
//

#include <cstdio>
#include <fstream>
#include <string>
#include <cstdlib>

#include "ParticleEngine.hpp"

static float get_random_value ()
{
    return (float)rand() / (float)RAND_MAX;
}

ParticleEngine::ParticleEngine (SDL_Renderer * renderer, Plot * plot):
    renderer { renderer },
    plot     { plot },

    gpu_device        { get_gpu_device() },
    opencl_context    { gpu_device },
    gpu_program       { create_and_build_gpu_program(opencl_context) },
    gpu_kernel        { gpu_program, "update_particle_data" },
    gpu_command_queue { opencl_context, gpu_device },

    random_numbers_buffer       { opencl_context, CL_MEM_ALLOC_HOST_PTR, constants::particle::count * sizeof(float) },
    random_number_flags_buffer  { opencl_context, CL_MEM_ALLOC_HOST_PTR, constants::particle::count * sizeof(bool) },
    cartesian_positions_buffer  { opencl_context, CL_MEM_ALLOC_HOST_PTR, opencl_buffer_size },
    graphical_positions_buffer  { opencl_context, CL_MEM_ALLOC_HOST_PTR, opencl_buffer_size }
{
    gpu_kernel.setArg(OpenCLKernelArguments::random_numbers_buffer,              random_numbers_buffer);
    gpu_kernel.setArg(OpenCLKernelArguments::random_number_flags_buffer,         random_number_flags_buffer);
    gpu_kernel.setArg(OpenCLKernelArguments::particle_cartesian_position_buffer, cartesian_positions_buffer);
    gpu_kernel.setArg(OpenCLKernelArguments::particle_graphical_position_buffer, graphical_positions_buffer);
    gpu_kernel.setArg(OpenCLKernelArguments::particle_count,                     constants::particle::count);
    gpu_kernel.setArg(OpenCLKernelArguments::window_size,                        (int)constants::window_size);

    random_numbers = (float *)gpu_command_queue.enqueueMapBuffer(
            random_numbers_buffer,
            CL_TRUE,
            CL_MAP_WRITE,
            0,
            constants::particle::count * sizeof(long)
            );

    random_number_flags = (bool *)gpu_command_queue.enqueueMapBuffer(
            random_number_flags_buffer,
            CL_TRUE,
            CL_MAP_WRITE,
            0,
            constants::particle::count * sizeof(bool)
            );

    for (int i = 0; i < constants::particle::count; i++)
    {
        random_numbers[i] = get_random_value();
        random_number_flags[i] = false;
    }

    gpu_command_queue.enqueueUnmapMemObject(random_numbers_buffer, random_numbers);
    gpu_command_queue.enqueueUnmapMemObject(random_number_flags_buffer, random_number_flags);
}

void ParticleEngine::update (size_t particle_count, SDL_FPoint cartesian_viewport_origin, int viewport_range)
{
    gpu_kernel.setArg(OpenCLKernelArguments::viewport_origin, cartesian_viewport_origin);
    gpu_kernel.setArg(OpenCLKernelArguments::viewport_range, viewport_range);

    gpu_command_queue.enqueueNDRangeKernel(gpu_kernel, cl::NullRange, cl::NDRange { particle_count });

    random_numbers = (float *)gpu_command_queue.enqueueMapBuffer(
            random_numbers_buffer,
            CL_TRUE,
            CL_MAP_WRITE,
            0,
            constants::particle::count * sizeof(float)
            );

    random_number_flags = (bool *)gpu_command_queue.enqueueMapBuffer(
            random_number_flags_buffer,
            CL_TRUE,
            CL_MAP_READ | CL_MAP_WRITE,
            0,
            constants::particle::count * sizeof(bool)
            );

    for (size_t i = 0; i < constants::particle::count; i++)
    {
        if (random_number_flags[i])
        {
            random_numbers[i] = get_random_value();
            random_number_flags[i] = false;
        }
    }

    gpu_command_queue.enqueueUnmapMemObject(random_numbers_buffer, random_numbers);
    gpu_command_queue.enqueueUnmapMemObject(random_number_flags_buffer, random_number_flags);
}

void ParticleEngine::draw ()
{
    graphical_positions = (SDL_FPoint *)gpu_command_queue.enqueueMapBuffer(
            graphical_positions_buffer,
            CL_TRUE,
            CL_MAP_READ,
            0,
            opencl_buffer_size
            );

    SDL_FRect particle_rects [constants::particle::count];

    for (size_t i = 0; i < constants::particle::count; i++)
        particle_rects[i] = { graphical_positions[i].x - 1.5f, graphical_positions[i].y - 1.5f, 3, 3 };

    SDL_SetRenderDrawColor(renderer, 100, 100, 100, 255);
    SDL_RenderFillRectsF(renderer, particle_rects, constants::particle::count);

    gpu_command_queue.enqueueUnmapMemObject(graphical_positions_buffer, graphical_positions);
}

cl::Device ParticleEngine::get_gpu_device ()
{
    std::vector<cl::Platform> platforms;
    cl::Platform::get(&platforms);

    if (platforms.empty())
    {
        std::printf("[OpenCL] No platforms found");
        exit(1);
    }

    cl::Platform platform = platforms[0];

    std::vector<cl::Device> platform_devices;
    platform.getDevices(CL_DEVICE_TYPE_GPU, &platform_devices);

    if (platform_devices.empty())
    {
        std::printf("[OpenCL] No devices found");
        exit(1);
    }

    return platform_devices[0];
}

cl::Program ParticleEngine::create_and_build_gpu_program (cl::Context const & opencl_context)
{
    cl::Device gpu_device = get_gpu_device();

    std::ifstream kernel_file_data { OPENCL_KERNEL_FILE_PATH };

    std::string kernel_file_contents {
            std::istreambuf_iterator<char>(kernel_file_data),
            (std::istreambuf_iterator<char>())
    };

    cl::Program::Sources program_sources { kernel_file_contents };

    cl::Program program { opencl_context, program_sources };
    cl_int build_status = program.build();

    std::printf("[OpenCL] program build for GPU: %s;\n", gpu_device.getInfo<CL_DEVICE_NAME>().c_str());

    if (build_status != CL_BUILD_SUCCESS)
    {
        std::printf("Failed\n%s", program.getBuildInfo<CL_PROGRAM_BUILD_LOG>(gpu_device).c_str());
        exit(1);
    }
    else
        std::printf("Success\n");

    return program;
}