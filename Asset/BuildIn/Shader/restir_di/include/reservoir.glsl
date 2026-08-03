// https://zhuanlan.zhihu.com/p/703950102

struct LightSample 
{
	// 被采样光源的信息
	uint lightID; 				// 光源索引
};

struct DIReservoir 
{
	LightSample sampl;

	float pHat;					// 当前光源的重要性采样权重，也就是targetPdf
	float sumWeights;			// 已处理的(pHat / proposalPdf)权重和
	float w;					// 被积函数在当前采样点对应的权重，同时也就是重采样重要性采样的realPdf(SIR PDF)的倒数
	uint numStreamSamples;		// 已处理的采样总数，M 
};

uint ReservoirIndex(ivec2 pixel)
{
	return pixel.y * WINDOW_WIDTH + pixel.x;
}

void CleanDIReservoir(inout DIReservoir res)
{
	res.sampl.lightID = 0;
	res.pHat = 0.0f;
	res.sumWeights = 0.0f;
	res.w = 0.0f;
}

DIReservoir NewDIReservoir() 
{
	DIReservoir result;
	CleanDIReservoir(result);
	result.numStreamSamples = 0;

	return result;
}

void UpdateDIReservoir(
	inout DIReservoir res, float weight, 
	uint lightID,
	float pHat, 
	inout Rand rand) 
{
	res.sumWeights += weight;											// 更新总计权重	
	if (RandFloat(rand) < (weight / (res.sumWeights + 0.00001))) 		// 按Reservoir更新原则更新样本
	{										
		res.sampl.lightID = lightID;
		res.pHat = pHat;
	}
}

void AddSampleToDIReservoir(
	inout DIReservoir res, 
	uint lightID, 
	float pHat, float proposalPdf, 
	inout Rand rand) 
{
	res.numStreamSamples = max(1, res.numStreamSamples + 1);						// 更新已处理采样数

	if(proposalPdf <= 0) return;

	float weight = pHat / proposalPdf;												// 重要性重采样的权重，pdf/新分布下采样该点的概率
	UpdateDIReservoir(
		res, weight, 
		lightID, 
		pHat, 
		rand);

	if(res.pHat <= 0) CleanDIReservoir(res);	
	else res.w = (1 / res.pHat) * ((res.sumWeights) / (res.numStreamSamples));		// 被积函数对应的权重
		
}

// MIS For Proposals：
// 保持targetPdf不变，使用来自不同proposalPdf的样本，样本权重仍为各自的targetPdf(x) / proposalPdf_i(x), 
// 在每个proposalPdf分布同域时，合并无偏的

void CombineDIReservoirs(
	inout DIReservoir self, 
	DIReservoir other, 
	float pHat, 
	inout Rand rand) 
{
	self.numStreamSamples += other.numStreamSamples;

	float weight = pHat * other.w * other.numStreamSamples;	// 将临近reservoir也视作一个（numStreamSamples个）样本：
	UpdateDIReservoir(														// proposalPdf是临近点的realPdf（SIR PDF）
		self, weight,													// targetPdf是待合并点的targetPdf
		other.sampl.lightID, 
		pHat, 
		rand);

	if(self.pHat <= 0) CleanDIReservoir(self);
	else self.w = (1.0 / self.pHat) * (1.0 / self.numStreamSamples) * self.sumWeights;
}