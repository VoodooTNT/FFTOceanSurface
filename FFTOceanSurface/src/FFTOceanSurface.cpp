#include "Common.hpp"

#include <random>

std::default_random_engine generator;
std::normal_distribution<float> normal_dist;

const int N = 512;
const int L = 1000;
const int BUTTERFLY_STEPS = std::log2(N);
const int LOCAL_WORK_GROUP_SIZE = 32;
const int GLOBAL_WORK_GROUP_SIZE = N / LOCAL_WORK_GROUP_SIZE;

const float G = 9.81;
const float A = 3e-7f;//phillips谱参数，影响波浪高度
const float PI = 3.141592653589;
const float V = 30.0;//风速
const glm::vec2 WindDir = glm::vec2(1.0, 1.0);//风向
const float l = 0.1;

glm::vec2 func_h_twiddle_0(glm::vec2 vec_k);
float func_P_h(glm::vec2 vec_k);
int bitreverse(int bit, int in);

int main()
{
	auto window = initWindow();

	Ogle::Shader* heightShader = new Ogle::Shader("./shader/GenerateHeightSpectrum.comp");
	Ogle::Shader* displacementShader = new Ogle::Shader("./shader/GenerateXZDisplacement.comp");
	Ogle::Shader* normalShader = new Ogle::Shader("./shader/GenerateNormal.comp");
	Ogle::Shader* fftHorShader = new Ogle::Shader("./shader/FFTHorizontal.comp");
	Ogle::Shader* fftVerShader = new Ogle::Shader("./shader/FFTVertical.comp");
	Ogle::Shader* resultShader = new Ogle::Shader("./shader/GeneratePositionNormal.comp");
	Ogle::Shader* drawShader = new Ogle::Shader("./shader/OceanSurface.vert", "./shader/OceanSurface.frag");

	float* vertices = new float[N * N * 5];
	float sX = -L / 2.0;
	float sZ = -L / 2.0;
	float vX = 0.0;
	float vY = 0.0;
	for (int i = 0; i < N; i++)
	{
		for (int j = 0; j < N; j++)
		{
			vertices[i * N * 5 + j * 5] = sX;
			vertices[i * N * 5 + j * 5 + 1] = 0.0;
			vertices[i * N * 5 + j * 5 + 2] = sZ;

			vertices[i * N * 5 + j * 5 + 3] = vX;
			vertices[i * N * 5 + j * 5 + 4] = vY;

			sX += (float)L / (float)(N - 1);

			vX += 1.0 / (N - 1);
		}
		sX = -L / 2.0;
		sZ += (float)L / (float)(N - 1);

		vX = 0.0;
		vY += 1.0 / (N - 1);
	}

	unsigned int* indices = new unsigned int[(N - 1) * (N - 1) * 6];
	for (int i = 0; i < N - 1; i++)
	{
		for (int j = 0; j < N - 1; j++)
		{
			indices[i * (N - 1) * 6 + j * 6] = i * N + j;
			indices[i * (N - 1) * 6 + j * 6 + 1] = i * N + j + 1;
			indices[i * (N - 1) * 6 + j * 6 + 2] = (i + 1) * N + j;
			indices[i * (N - 1) * 6 + j * 6 + 3] = i * N + j + 1;
			indices[i * (N - 1) * 6 + j * 6 + 4] = (i + 1) * N + j;
			indices[i * (N - 1) * 6 + j * 6 + 5] = (i + 1) * N + j + 1;
		}
	}

	unsigned int VAO;
	glGenVertexArrays(1, &VAO);
	glBindVertexArray(VAO);
	unsigned int VBO;
	glGenBuffers(1, &VBO);
	glBindBuffer(GL_ARRAY_BUFFER, VBO);
	glBufferData(GL_ARRAY_BUFFER, N * N * 5 * 4, vertices, GL_STATIC_DRAW);

	unsigned int EBO;
	glGenBuffers(1, &EBO);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, (N - 1) * (N - 1) * 6 * 4, indices, GL_STATIC_DRAW);

	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
	glEnableVertexAttribArray(1);



	///////////////////////////////////////////////////////////////
	generator.seed(time(NULL));

	float *h0Data = new float[N * N * 2];
	float *h0ConjData = new float[N * N * 2];
	for (int i = 0; i < N; i++) 
	{
		for (int j = 0; j < N; j++) 
		{
			glm::vec2 k = glm::vec2(2 * PI * (i - N / 2) / L, 2 * PI * (j - N / 2) / L);

			glm::vec2 hTilde0 = func_h_twiddle_0(k);
			glm::vec2 hTilde0Conj = func_h_twiddle_0(k);
			hTilde0Conj.y *= -1.0f;//共轭所以y为负

			h0Data[i * 2 * N + j * 2] = hTilde0.x;
			h0Data[i * 2 * N + j * 2 + 1] = hTilde0.y;

			h0ConjData[i * 2 * N + j * 2] = hTilde0Conj.x;
			h0ConjData[i * 2 * N + j * 2 + 1] = hTilde0Conj.y;
		}
	}

	//存储H0
	unsigned int g_textureH0;
	glGenTextures(1, &g_textureH0);
	//glActiveTexture(GL_TEXTURE0); 
	glBindTexture(GL_TEXTURE_2D, g_textureH0);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RG32F, N, N, 0, GL_RG, GL_FLOAT, h0Data);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	//存储H0Conj
	unsigned int g_textureH0Conj;
	glGenTextures(1, &g_textureH0Conj);
	glBindTexture(GL_TEXTURE_2D, g_textureH0Conj);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RG32F, N, N, 0, GL_RG, GL_FLOAT, h0ConjData);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	//存储Ht
	unsigned int g_textureHt;
	glGenTextures(1, &g_textureHt);
	glBindTexture(GL_TEXTURE_2D, g_textureHt);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RG32F, N, N, 0, GL_RG, GL_FLOAT, 0);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	//存储xz偏移图谱
	unsigned int g_textureDisplacement[2];
	glGenTextures(1, &g_textureDisplacement[0]);//x
	glBindTexture(GL_TEXTURE_2D, g_textureDisplacement[0]);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RG32F, N, N, 0, GL_RG, GL_FLOAT, 0);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glGenTextures(1, &g_textureDisplacement[1]);//z
	glBindTexture(GL_TEXTURE_2D, g_textureDisplacement[1]);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RG32F, N, N, 0, GL_RG, GL_FLOAT, 0);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	//存储IFFT后的xyz偏移结果
	unsigned int g_textureResult[3];
	glGenTextures(1, &g_textureResult[0]);//x
	glBindTexture(GL_TEXTURE_2D, g_textureResult[0]);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RG32F, N, N, 0, GL_RG, GL_FLOAT, 0);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glGenTextures(1, &g_textureResult[1]);//y
	glBindTexture(GL_TEXTURE_2D, g_textureResult[1]);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RG32F, N, N, 0, GL_RG, GL_FLOAT, 0);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glGenTextures(1, &g_textureResult[2]);//z
	glBindTexture(GL_TEXTURE_2D, g_textureResult[2]);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RG32F, N, N, 0, GL_RG, GL_FLOAT, 0);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	//临时存储
	unsigned int g_textureTemp;
	glGenTextures(1, &g_textureTemp);
	glBindTexture(GL_TEXTURE_2D, g_textureTemp);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RG32F, N, N, 0, GL_RG, GL_FLOAT, 0);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	//存储xz法线分量
	unsigned int g_textureXZNormal[2];
	glGenTextures(1, &g_textureXZNormal[0]);//x
	glBindTexture(GL_TEXTURE_2D, g_textureXZNormal[0]);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RG32F, N, N, 0, GL_RG, GL_FLOAT, 0);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glGenTextures(1, &g_textureXZNormal[1]);//z
	glBindTexture(GL_TEXTURE_2D, g_textureXZNormal[1]);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RG32F, N, N, 0, GL_RG, GL_FLOAT, 0);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	//存储偏移结果值
	unsigned int g_texturePosition;
	glGenTextures(1, &g_texturePosition);
	glBindTexture(GL_TEXTURE_2D, g_texturePosition);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, N, N, 0, GL_RGBA, GL_FLOAT, 0);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	//存储法线向量值
	unsigned int g_textureNormal;
	glGenTextures(1, &g_textureNormal);
	glBindTexture(GL_TEXTURE_2D, g_textureNormal);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, N, N, 0, GL_RGBA, GL_FLOAT, 0);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	
	//使用GPU计算FFT需要先把第一次迭代的索引计算好例：N=8时 索引为(0,4) (2,6) (1,5) (3,7)
	float *butterflyIndices = new float[N];
	for (int i = 0; i < N; i++)
	{
		butterflyIndices[i] = bitreverse(BUTTERFLY_STEPS, i);
	}

	//将计算好的索引值存储到贴图当中
	unsigned int g_textureIndices;
	glGenTextures(1, &g_textureIndices);
	glBindTexture(GL_TEXTURE_1D, g_textureIndices);
	glTexImage1D(GL_TEXTURE_1D, 0, GL_R32F, N, 0, GL_RED, GL_FLOAT, butterflyIndices);
	glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	///////////////////////////////////////////////////////////////



	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindVertexArray(0);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

	glEnable(GL_DEPTH_TEST);

	float time_ = 0.0;

	while (!glfwWindowShouldClose(window))
	{
		float currentFrame = static_cast<float>(glfwGetTime());
		deltaTime = currentFrame - lastFrame;
		lastFrame = currentFrame;

		processInput(window);

		glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);//同时清除深度缓冲区

		time_ += 0.02;

		/////////////////////////////////////////////////////////////////////
		//使用HeightSpectrum计算着色器计算高度
		glUseProgram(heightShader->id);
		glBindImageTexture(0, g_textureH0, 0, GL_FALSE, 0, GL_READ_ONLY, GL_RG32F);
		glBindImageTexture(1, g_textureH0Conj, 0, GL_FALSE, 0, GL_READ_ONLY, GL_RG32F);
		glBindImageTexture(2, g_textureHt, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RG32F);
		//传递运行时间
		glUniform1f(glGetUniformLocation(heightShader->id, "time"), time_);
		//创建一个N*N大小的工作组，即同时计算所有的顶点高度值
		glDispatchCompute(GLOBAL_WORK_GROUP_SIZE, GLOBAL_WORK_GROUP_SIZE, 1);
		//确保所有的数据都写入到贴图里了
		glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

		//使用XZDisplacement计算着色器计算xz方向偏移
		glUseProgram(displacementShader->id);
		glBindImageTexture(0, g_textureHt, 0, GL_FALSE, 0, GL_READ_ONLY, GL_RG32F);
		glBindImageTexture(1, g_textureDisplacement[0], 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RG32F);
		glBindImageTexture(2, g_textureDisplacement[1], 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RG32F);
		//创建一个N*N大小的工作组，即同时计算所有的顶点高度值
		glDispatchCompute(GLOBAL_WORK_GROUP_SIZE, GLOBAL_WORK_GROUP_SIZE, 1);
		//确保所有的数据都写入到贴图里了
		glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

		//使用XZNormal计算着色器计算xz法线
		glUseProgram(normalShader->id);
		glBindImageTexture(0, g_textureHt, 0, GL_FALSE, 0, GL_READ_ONLY, GL_RG32F);
		glBindImageTexture(1, g_textureXZNormal[0], 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RG32F);
		glBindImageTexture(2, g_textureXZNormal[1], 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RG32F);
		//创建一个N*N大小的工作组，即同时计算所有的顶点高度值
		glDispatchCompute(GLOBAL_WORK_GROUP_SIZE, GLOBAL_WORK_GROUP_SIZE, 1);
		//确保所有的数据都写入到贴图里了
		glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

		//使用FFT计算着色器对结果进行逆变换
		//y
		//Horizontal
		glUseProgram(fftHorShader->id);
		glUniform1i(glGetUniformLocation(fftHorShader->id, "u_steps"), BUTTERFLY_STEPS);
		//绑定索引贴图、上个计算着色器所得结果的波浪函数贴图、将要存储偏移值的贴图
		glBindImageTexture(0, g_textureHt, 0, GL_FALSE, 0, GL_READ_ONLY, GL_RG32F);
		glBindImageTexture(1, g_textureTemp, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RG32F);
		glBindImageTexture(2, g_textureIndices, 0, GL_FALSE, 0, GL_READ_ONLY, GL_R32F);
		// 先对每一行进行逆变换
		glDispatchCompute(1, N, 1);
		glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

		//Vertical
		glUseProgram(fftVerShader->id);
		glUniform1i(glGetUniformLocation(fftVerShader->id, "u_steps"), BUTTERFLY_STEPS);
		//绑定索引贴图、上个计算着色器所得结果的波浪函数贴图、将要存储偏移值的贴图
		glBindImageTexture(0, g_textureTemp, 0, GL_FALSE, 0, GL_READ_ONLY, GL_RG32F);
		glBindImageTexture(1, g_textureResult[1], 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RG32F);
		glBindImageTexture(2, g_textureIndices, 0, GL_FALSE, 0, GL_READ_ONLY, GL_R32F);
		// 再对每一列进行逆变换
		glDispatchCompute(N, 1, 1);
		glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

		//使用FFT计算着色器对结果进行逆变换
		//x
		//Horizontal
		glUseProgram(fftHorShader->id);
		glUniform1i(glGetUniformLocation(fftHorShader->id, "u_steps"), BUTTERFLY_STEPS);
		//绑定索引贴图、上个计算着色器所得结果的波浪函数贴图、将要存储偏移值的贴图
		glBindImageTexture(0, g_textureDisplacement[0], 0, GL_FALSE, 0, GL_READ_ONLY, GL_RG32F);
		glBindImageTexture(1, g_textureTemp, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RG32F);
		glBindImageTexture(2, g_textureIndices, 0, GL_FALSE, 0, GL_READ_ONLY, GL_R32F);
		// 先对每一行进行逆变换
		glDispatchCompute(1, N, 1);
		glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

		//Vertical
		glUseProgram(fftVerShader->id);
		glUniform1i(glGetUniformLocation(fftVerShader->id, "u_steps"), BUTTERFLY_STEPS);
		//绑定索引贴图、上个计算着色器所得结果的波浪函数贴图、将要存储偏移值的贴图
		glBindImageTexture(0, g_textureTemp, 0, GL_FALSE, 0, GL_READ_ONLY, GL_RG32F);
		glBindImageTexture(1, g_textureResult[0], 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RG32F);
		glBindImageTexture(2, g_textureIndices, 0, GL_FALSE, 0, GL_READ_ONLY, GL_R32F);
		// 再对每一列进行逆变换
		glDispatchCompute(N, 1, 1);
		glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

		//使用FFT计算着色器对结果进行逆变换
		//z
		//Horizontal
		glUseProgram(fftHorShader->id);
		glUniform1i(glGetUniformLocation(fftHorShader->id, "u_steps"), BUTTERFLY_STEPS);
		//绑定索引贴图、上个计算着色器所得结果的波浪函数贴图、将要存储偏移值的贴图
		glBindImageTexture(0, g_textureDisplacement[1], 0, GL_FALSE, 0, GL_READ_ONLY, GL_RG32F);
		glBindImageTexture(1, g_textureTemp, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RG32F);
		glBindImageTexture(2, g_textureIndices, 0, GL_FALSE, 0, GL_READ_ONLY, GL_R32F);
		// 先对每一行进行逆变换
		glDispatchCompute(1, N, 1);
		glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

		//Vertical
		glUseProgram(fftVerShader->id);
		glUniform1i(glGetUniformLocation(fftVerShader->id, "u_steps"), BUTTERFLY_STEPS);
		//绑定索引贴图、上个计算着色器所得结果的波浪函数贴图、将要存储偏移值的贴图
		glBindImageTexture(0, g_textureTemp, 0, GL_FALSE, 0, GL_READ_ONLY, GL_RG32F);
		glBindImageTexture(1, g_textureResult[2], 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RG32F);
		glBindImageTexture(2, g_textureIndices, 0, GL_FALSE, 0, GL_READ_ONLY, GL_R32F);
		// 再对每一列进行逆变换
		glDispatchCompute(N, 1, 1);
		glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

		//使用FFT计算着色器对结果进行逆变换
		//Nx
		//Horizontal
		glUseProgram(fftHorShader->id);
		glUniform1i(glGetUniformLocation(fftHorShader->id, "u_steps"), BUTTERFLY_STEPS);
		//绑定索引贴图、上个计算着色器所得结果的波浪函数贴图、将要存储偏移值的贴图
		glBindImageTexture(0, g_textureXZNormal[0], 0, GL_FALSE, 0, GL_READ_ONLY, GL_RG32F);
		glBindImageTexture(1, g_textureTemp, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RG32F);
		glBindImageTexture(2, g_textureIndices, 0, GL_FALSE, 0, GL_READ_ONLY, GL_R32F);
		// 先对每一行进行逆变换
		glDispatchCompute(1, N, 1);
		glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

		//Vertical
		glUseProgram(fftVerShader->id);
		glUniform1i(glGetUniformLocation(fftVerShader->id, "u_steps"), BUTTERFLY_STEPS);
		//绑定索引贴图、上个计算着色器所得结果的波浪函数贴图、将要存储偏移值的贴图
		glBindImageTexture(0, g_textureTemp, 0, GL_FALSE, 0, GL_READ_ONLY, GL_RG32F);
		glBindImageTexture(1, g_textureXZNormal[0], 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RG32F);
		glBindImageTexture(2, g_textureIndices, 0, GL_FALSE, 0, GL_READ_ONLY, GL_R32F);
		// 再对每一列进行逆变换
		glDispatchCompute(N, 1, 1);
		glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

		//使用FFT计算着色器对结果进行逆变换
		//Nz
		//Horizontal
		glUseProgram(fftHorShader->id);
		glUniform1i(glGetUniformLocation(fftHorShader->id, "u_steps"), BUTTERFLY_STEPS);
		//绑定索引贴图、上个计算着色器所得结果的波浪函数贴图、将要存储偏移值的贴图
		glBindImageTexture(0, g_textureXZNormal[1], 0, GL_FALSE, 0, GL_READ_ONLY, GL_RG32F);
		glBindImageTexture(1, g_textureTemp, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RG32F);
		glBindImageTexture(2, g_textureIndices, 0, GL_FALSE, 0, GL_READ_ONLY, GL_R32F);
		// 先对每一行进行逆变换
		glDispatchCompute(1, N, 1);
		glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

		//Vertical
		glUseProgram(fftVerShader->id);
		glUniform1i(glGetUniformLocation(fftVerShader->id, "u_steps"), BUTTERFLY_STEPS);
		//绑定索引贴图、上个计算着色器所得结果的波浪函数贴图、将要存储偏移值的贴图
		glBindImageTexture(0, g_textureTemp, 0, GL_FALSE, 0, GL_READ_ONLY, GL_RG32F);
		glBindImageTexture(1, g_textureXZNormal[1], 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RG32F);
		glBindImageTexture(2, g_textureIndices, 0, GL_FALSE, 0, GL_READ_ONLY, GL_R32F);
		// 再对每一列进行逆变换
		glDispatchCompute(N, 1, 1);
		glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

		//更新结果
		glUseProgram(resultShader->id);
		glBindImageTexture(0, g_textureResult[0], 0, GL_FALSE, 0, GL_READ_ONLY, GL_RG32F);
		glBindImageTexture(1, g_textureResult[1], 0, GL_FALSE, 0, GL_READ_ONLY, GL_RG32F);
		glBindImageTexture(2, g_textureResult[2], 0, GL_FALSE, 0, GL_READ_ONLY, GL_RG32F);
		glBindImageTexture(3, g_textureXZNormal[0], 0, GL_FALSE, 0, GL_READ_ONLY, GL_RG32F);
		glBindImageTexture(4, g_textureXZNormal[1], 0, GL_FALSE, 0, GL_READ_ONLY, GL_RG32F);
		glBindImageTexture(5, g_texturePosition, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F);
		glBindImageTexture(6, g_textureNormal, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F);
		glDispatchCompute(GLOBAL_WORK_GROUP_SIZE, GLOBAL_WORK_GROUP_SIZE, 1);
		glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
		/////////////////////////////////////////////////////////////////////

		glUseProgram(drawShader->id);
		
		glUniform3f(glGetUniformLocation(drawShader->id, "mLight.position"), 100.0, 300.0, 100.0);
		//glUniform3f(glGetUniformLocation(drawShader->id, "mLight.direction"), 100.0, 300.0, 100.0);
		glUniform3f(glGetUniformLocation(drawShader->id, "mLight.ambient"), 1.0, 1.0, 1.0);
		glUniform3f(glGetUniformLocation(drawShader->id, "mLight.diffuse"), 1.0, 1.0, 1.0);
		glUniform3f(glGetUniformLocation(drawShader->id, "mLight.specular"), 1.0, 1.0, 1.0);
		glUniform3f(glGetUniformLocation(drawShader->id, "viewPos"), camera.Position.x, camera.Position.y, camera.Position.z);

		glm::mat4 model = glm::mat4(1.0);
		glm::mat4 view = glm::mat4(1.0);
		glm::mat4 projection = glm::mat4(1.0);
		//model = glm::translate(model, glm::vec3(0.0, 0.0, 3.0));//对于Z轴，靠近人为正
		//model = glm::rotate(model, glm::radians(55.0f), glm::vec3(1.0, 0.0, 0.0));
		view = camera.GetViewMatrix();
		projection = glm::perspective(glm::radians(camera.Zoom), (float)SCR_WIDTH / (float)SCR_HEIGHT, 0.1f, 10000.0f);
		glUniformMatrix4fv(glGetUniformLocation(drawShader->id, "model"), 1, GL_FALSE, glm::value_ptr(model));
		glUniformMatrix4fv(glGetUniformLocation(drawShader->id, "view"), 1, GL_FALSE, glm::value_ptr(view));
		glUniformMatrix4fv(glGetUniformLocation(drawShader->id, "projection"), 1, GL_FALSE, glm::value_ptr(projection));

		/////////////////////////////////////////////////////////////////////
		//将波浪高度贴图绑定在GL_TEXTURE0
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, g_texturePosition);
		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, g_textureNormal);
		/////////////////////////////////////////////////////////////////////

		glBindVertexArray(VAO);
		//glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
		glDrawElements(GL_TRIANGLES, (N - 1) * (N - 1) * 6, GL_UNSIGNED_INT, (void*)0);

		glfwSwapBuffers(window);
		glfwPollEvents();
	}

	glDeleteVertexArrays(1, &VAO);
	glDeleteBuffers(1, &VBO);
	glDeleteBuffers(1, &EBO);
	glDeleteProgram(heightShader->id);
	glDeleteProgram(displacementShader->id);
	glDeleteProgram(normalShader->id);
	glDeleteProgram(fftHorShader->id);
	glDeleteProgram(fftVerShader->id);
	glDeleteProgram(resultShader->id);
	glDeleteProgram(drawShader->id);

	glfwTerminate();
	return 0;
}

glm::vec2 func_h_twiddle_0(glm::vec2 vec_k)
{
	float xi_r = normal_dist(generator);
	float xi_i = normal_dist(generator);
	return std::sqrt(0.5f) * glm::vec2(xi_r, xi_i) * std::sqrt(func_P_h(vec_k));
}

float func_P_h(glm::vec2 vec_k)
{
	if (vec_k == glm::vec2(0.0f, 0.0f))
		return 0.0f;

	float L = V * V / G;

	float k = glm::length(vec_k);
	glm::vec2 k_hat = glm::normalize(vec_k);

	float dot_k_hat_omega_hat = glm::dot(k_hat, glm::normalize(WindDir));
	float result = A * std::exp(-1 / (k * L * k * L)) / std::pow(k, 4) * std::pow(dot_k_hat_omega_hat, 2);

	result *= std::exp(-k * k * l * l);

	return result;
}

int bitreverse(int bit, int in)
{
	int* v = new int[bit];
	int t = in;
	for (int i = bit - 1; i >= 0; i--)
	{
		int s = t / 2;
		v[i] = t % 2;
		t = s;
	}

	t = 0;
	for (int j = bit - 1; j >= 0; j--)
	{
		if (v[j] == 1)
		{
			t += std::pow(2, j);
		}
	}

	delete[] v;
	return t;
}