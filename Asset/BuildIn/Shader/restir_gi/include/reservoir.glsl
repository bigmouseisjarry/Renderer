
struct HitSample 
{
	vec3 hitPos;
	float _padding0;
	vec3 hitNormal;
	float _padding1;
	vec3 outRadiance;
	float _padding2;
};

struct GIReservoir 
{
	HitSample sampl;

	float pHat;					// 当前命中点的重要性采样权重，也就是targetPdf	（outRadiance的luminance）
	float sumWeights;			// 已处理的(pHat / proposalPdf)权重和
	float w;					// 被积函数在当前采样点对应的权重，同时也就是重采样重要性采样的realPdf(SIR PDF)的倒数
	uint numStreamSamples;		// 已处理的采样总数，M 
};

uint ReservoirIndex(ivec2 pixel)	// 降分辨率像素到reservoir的索引
{
	return pixel.y * int(WINDOW_WIDTH / WIDTH_DOWNSAMPLE_RATE) + pixel.x;
}

void CleanGIReservoir(inout GIReservoir res)
{
	res.sampl.hitPos = vec3(0.0f);
	res.sampl.hitNormal = vec3(0.0f);
	res.sampl.outRadiance = vec3(0.0f);
	
	res.pHat = 0.0f;
	res.sumWeights = 0.0f;
	res.w = 0.0f;
}

GIReservoir NewGIReservoir() 
{
	GIReservoir result;
	CleanGIReservoir(result);
	result.numStreamSamples = 0;

	return result;
}

void UpdateGIReservoir(
	inout GIReservoir res, float weight, 
	in HitSample hitSample,
	float pHat, 
	inout Rand rand) 
{
	res.sumWeights += weight;											// 更新总计权重	
	if (RandFloat(rand) < (weight / (res.sumWeights + 0.00001))) 		// 按Reservoir更新原则更新样本
	{										
		res.sampl = hitSample;
		res.pHat = pHat;
	}
}

void AddSampleToGIReservoir(
	inout GIReservoir res, 
	in HitSample hitSample,
	float pHat, float proposalPdf, 
	inout Rand rand) 
{
	res.numStreamSamples = max(1, res.numStreamSamples + 1);						// 更新已处理采样数

	if(proposalPdf <= 0) return;

	float weight = pHat / proposalPdf;												// 重要性重采样的权重，pdf/新分布下采样该点的概率
	UpdateGIReservoir(
		res, weight, 
		hitSample, 
		pHat, 
		rand);

	if(res.pHat <= 0) CleanGIReservoir(res);	
	else res.w = (1 / res.pHat) * ((res.sumWeights) / (res.numStreamSamples));		// 被积函数对应的权重
		
}

// MIS For Proposals：
// 保持targetPdf不变，使用来自不同proposalPdf的样本，样本权重仍为各自的targetPdf(x) / proposalPdf_i(x), 
// 在每个proposalPdf分布同域时，合并无偏的

void CombineGIReservoirs(
	inout GIReservoir self, 
	GIReservoir other, 
	float pHat, 
	inout Rand rand) 
{
	self.numStreamSamples += other.numStreamSamples;

	float weight = pHat * other.w * other.numStreamSamples;	// 将临近reservoir也视作一个（numStreamSamples个）样本：
	UpdateGIReservoir(														// proposalPdf是临近点的realPdf（SIR PDF）
		self, weight,													// targetPdf是待合并点的targetPdf
		other.sampl, 
		pHat, 
		rand);

	if(self.pHat <= 0) CleanGIReservoir(self);
	else self.w = (1.0 / self.pHat) * (1.0 / self.numStreamSamples) * self.sumWeights;
}