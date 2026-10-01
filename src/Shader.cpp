#include "Shader.hpp"
#include <volk/volk.h>
#include <slang/slang.h>
#include <slang/slang-com-ptr.h>
#include <array>
#include <iostream>
#include <stdexcept>

Shader::Shader(VkDevice device, const std::filesystem::path& path) : device_(device)
{
    Slang::ComPtr<slang::IGlobalSession> slangGlobalSession_;
    if (SLANG_FAILED(slang::createGlobalSession(slangGlobalSession_.writeRef()))) {
		std::cerr << "Could not initialize the Slang compiler\n";
		throw std::runtime_error("Shader compilation failed: " + path.string());
	}

    auto slangTargets{ std::to_array<slang::TargetDesc>({ {
        .format{SLANG_SPIRV},
        .profile{slangGlobalSession_->findProfile("spirv_1_4")}
    } })};
    auto slangOptions{ std::to_array<slang::CompilerOptionEntry>({ {
        slang::CompilerOptionName::EmitSpirvDirectly,
        {slang::CompilerOptionValueKind::Int, 1}
    } })};
    slang::SessionDesc slangSessionDesc{
        .targets{slangTargets.data()},
        .targetCount{SlangInt(slangTargets.size())},
        .defaultMatrixLayoutMode = SLANG_MATRIX_LAYOUT_COLUMN_MAJOR,
        .compilerOptionEntries{slangOptions.data()},
        .compilerOptionEntryCount{uint32_t(slangOptions.size())}
    };

    Slang::ComPtr<slang::ISession> slangSession;
	if (SLANG_FAILED(slangGlobalSession_->createSession(slangSessionDesc, slangSession.writeRef()))) {
		std::cerr << "Could not create the Slang session\n";
		throw std::runtime_error("Shader compilation failed: " + path.string());
	}

	Slang::ComPtr<ISlangBlob> diagnostics;
	Slang::ComPtr<slang::IModule> slangModule{ slangSession->loadModule(path.string().c_str(), diagnostics.writeRef()) };
	if (diagnostics) {
		std::cerr << static_cast<const char*>(diagnostics->getBufferPointer());
	}
	if (!slangModule) {
		std::cerr << "Could not load shader source\n";
		throw std::runtime_error("Shader compilation failed: " + path.string());
	}
	Slang::ComPtr<ISlangBlob> spirv;
	diagnostics.setNull();
	SlangResult compileResult = slangModule->getTargetCode(0, spirv.writeRef(), diagnostics.writeRef());
	if (diagnostics) {
		std::cerr << static_cast<const char*>(diagnostics->getBufferPointer());
	}
	if (SLANG_FAILED(compileResult) || !spirv) {
		std::cerr << "Could not compile shader source to SPIR-V\n";
		throw std::runtime_error("Shader compilation failed: " + path.string());
	}

    VkShaderModuleCreateInfo shaderModuleCI{ .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, .codeSize = spirv->getBufferSize(), .pCode = (uint32_t*)spirv->getBufferPointer() };
    const VkResult result = vkCreateShaderModule(device_, &shaderModuleCI, nullptr, &module_);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Could not create shader module for " + path.string() +
                                 ": VkResult " + std::to_string(result));
    }
}

Shader::~Shader()
{
    if (module_ != VK_NULL_HANDLE) {
        vkDestroyShaderModule(device_, module_, nullptr);
    }
}
