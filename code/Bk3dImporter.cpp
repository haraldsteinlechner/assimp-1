/*
---------------------------------------------------------------------------
Open Asset Import Library (assimp)
---------------------------------------------------------------------------

Copyright (c) 2006-2012, assimp team

All rights reserved.

Redistribution and use of this software in source and binary forms,
with or without modification, are permitted provided that the following
conditions are met:

* Redistributions of source code must retain the above
copyright notice, this list of conditions and the
following disclaimer.

* Redistributions in binary form must reproduce the above
copyright notice, this list of conditions and the
following disclaimer in the documentation and/or other
materials provided with the distribution.

* Neither the name of the assimp team, nor the names of its
contributors may be used to endorse or promote products
derived from this software without specific prior
written permission of the assimp team.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
---------------------------------------------------------------------------
*/

/** @file  B3DImporter.cpp
*  @brief Implementation of the b3d importer class
*/



// internal headers
#include "Bk3dImporter.h"
#include "TextureTransform.h"
#include "ConvertToLHProcess.h"
#include <boost/scoped_ptr.hpp>
#include "../include/assimp/IOSystem.hpp"
#include "../include/assimp/anim.h"
#include "../include/assimp/scene.h"
#include "../include/assimp/DefaultLogger.hpp"
#include <iostream>
#include "MakeVerboseFormat.h"

using namespace Assimp;
using namespace std;

static const aiImporterDesc desc = {
	"BK3D Importer",
	"",
	"",
	"",
	aiImporterFlags_SupportBinaryFlavour,
	0,
	0,
	0,
	0,
	"bk3d.gz"
};

// (fixme, Aramis) quick workaround to get rid of all those signed to unsigned warnings
#ifdef _MSC_VER 
#	pragma warning (disable: 4018)
#endif

//#define DEBUG_B3D

Bk3dImporter::Bk3dImporter()
{

}

Bk3dImporter::~Bk3dImporter()
{
}

// ------------------------------------------------------------------------------------------------
bool Bk3dImporter::CanRead(const std::string& pFile, IOSystem* /*pIOHandler*/, bool /*checkSig*/) const{

	if (pFile.find("bk3d.gz") != std::string::npos) 
	{
		return bk3d::tryLoad(pFile.c_str());
	}
	return false;
}

// ------------------------------------------------------------------------------------------------
// Loader meta information
const aiImporterDesc* Bk3dImporter::GetInfo() const
{
	return &desc;
}

template<typename T> T* vecToArray(std::vector<T>& input)
{
	T* res = new T[input.size()];
	for (int i = 0; i < input.size(); i++)
		res[i] = input[i];
	return res;
}


#ifdef DEBUG_B3D
extern "C"{ void _stdcall AllocConsole(); }
#endif
// ------------------------------------------------------------------------------------------------
void Bk3dImporter::InternReadFile(const std::string& pFile, aiScene* pScene, IOSystem* pIOHandler){
	auto fileHeader = bk3d::load(pFile.c_str());
	auto meshes = vector<aiMesh*>();
	auto nodes = vector<aiNode*>();
	auto rootNode = new aiNode();
	for (int i = 0; i < fileHeader->pMeshes->n; i++)
	{
		auto mesh = fileHeader->pMeshes->p[i];
		auto newMesh = new aiMesh;
		newMesh->mName = mesh->name;

		bk3d::Attribute* normalsAttrib = nullptr;
		bk3d::Attribute* positions = nullptr;
		for (int a = 0;a < mesh->pAttributes->n;a++)
		{
			std::string name = mesh->pAttributes->p[a]->name;
			if (name.find("normal") != std::string::npos) normalsAttrib = mesh->pAttributes->p[a];
			else if (name.find("position") != std::string::npos) positions = mesh->pAttributes->p[a];
		}
 		assert(positions);

		auto sizeBytes = mesh->pSlots->p[positions->slot]->vtxBufferSizeBytes;

		//switch (positions->formatGL)
		//{
		//case GL_FLOAT:
		//	newMesh->mNumVertices = sizeBytes / (sizeof(float) * positions->numComp);
		//	break;
		//default: throw "oida";
		//}

		//newMesh->mVertices = new aiVector3D[newMesh->mNumVertices]; // copy since assimp deletes shared arrays otherwise
		//memcpy(newMesh->mVertices, mesh->pSlots->p[positions->slot]->pVtxBufferData, sizeBytes);

		void* data = mesh->pSlots->p[positions->slot]->pVtxBufferData;
		auto stride = mesh->pSlots->p[positions->slot]->vtxBufferStrideBytes;
		auto vertexCount = sizeBytes / stride;
		auto vertices = new aiVector3D[vertexCount];
		for (int v = 0; v < vertexCount;v++)
		{
			vertices[v] = *(aiVector3D*)data;
			data = (char*)data + stride;
			//data = (void*)((unsigned int)data + mesh->pSlots->p[positions->slot]->vtxBufferStrideBytes);
		}
		newMesh->mVertices = vertices;
		newMesh->mNumVertices = vertexCount;
		if (normalsAttrib)
		{
			auto normals = new aiVector3D[vertexCount];
			void* data = mesh->pSlots->p[normalsAttrib->slot]->pVtxBufferData;
			data = (char*)data + normalsAttrib->dataOffsetBytes;
			auto stride = mesh->pSlots->p[normalsAttrib->slot]->vtxBufferStrideBytes;
			for (int v = 0; v < vertexCount;v++)
			{
				normals[v] = *(aiVector3D*)data;
				data = (char*)data + stride;
				//data = (void*)((unsigned int)data + mesh->pSlots->p[positions->slot]->vtxBufferStrideBytes);
			}
			newMesh->mNormals = normals;
		} else {
			newMesh->mNormals = new aiVector3D[vertexCount];
			memset(newMesh->mNormals, 0, vertexCount*sizeof(aiVector3D));
		}
		
	
		auto indicesV = new std::vector<aiFace>();
		for (int pg = 0; pg < mesh->pPrimGroups->n; pg++)
		{
			bk3d::PrimGroup* pPG = mesh->pPrimGroups->p[pg];
			GLenum PGTopo = pPG->topologyGL;
			if (PGTopo != GL_TRIANGLES)
				cout << PGTopo << endl;

			unsigned int* indices;
			if (pPG->indexFormatGL == GL_UNSIGNED_SHORT)
			{
				indices = new unsigned int[pPG->indexCount];
				unsigned short* shortIndices = (unsigned short*)pPG->pIndexBufferData;
				for (int ci = 0;ci < pPG->indexCount;ci++)
				{
					indices[ci] = shortIndices[ci];
				}
			}
			else if (pPG->indexFormatGL == GL_UNSIGNED_INT)
			{
				indices = (unsigned int*)pPG->pIndexBufferData;
			}
			else if (pPG->indexFormatGL == 0)
			{
				indices = nullptr;
			}
			else throw "whot";
			switch (PGTopo)
			{
			case GL_TRIANGLES:
				if (pPG->indexArrayByteSize > 0)
				{
					for (int index = pPG->indexOffset;index < pPG->indexCount - 3;index += 3)
					{
						auto face = aiFace();
						face.mIndices = new unsigned int[3]{ indices[index], indices[index + 1], indices[index + 2] };
						face.mNumIndices = 3;
						if (indices[index] >= newMesh->mNumVertices ||
							indices[index + 1] >= newMesh->mNumVertices ||
							indices[index + 2] >= newMesh->mNumVertices)
						{
							throw "index out of range";
						}
						indicesV->push_back(face);
					}
				}
				else
				{
					for (unsigned int index = pPG->indexOffset;index < pPG->indexCount - 3;index += 3)
					{
						auto face = aiFace();
						face.mIndices = new unsigned int[3]{ index, index + 1, index + 2 };
						face.mNumIndices = 3;
						indicesV->push_back(face);
					}
				}
				break;
			case GL_TRIANGLE_STRIP:
				if (pPG->indexArrayByteSize > 0)
				{
					auto v0 = indices[pPG->indexOffset];
					auto v1 = indices[pPG->indexOffset+1];
					int i = 0;
					for (int index = pPG->indexOffset + 2;index < pPG->indexCount;index++,i++)
					{
						auto face = aiFace();
						if (v0 >= newMesh->mNumVertices ||
							v1 >= newMesh->mNumVertices ||
							indices[index] >= newMesh->mNumVertices)
						{
							throw "index out of range";
						}

						face.mIndices = new unsigned int[3]{ v0, v1, indices[index] };
						face.mNumIndices = 3;
						if (i % 2 == 0)
							v0 = indices[index];
						else
							v1 = indices[index];

						indicesV->push_back(face);
					}
				}
				else
				{
					int i = 0;
					unsigned int v0 = pPG->indexOffset;
					unsigned int v1 = pPG->indexOffset + 1;
					for (unsigned int index = pPG->indexOffset + 2;index < pPG->indexCount;index++,i++)
					{
						auto face = aiFace();
						face.mIndices = new unsigned int[3]{ v0, v1, index };
						face.mNumIndices = 3;
						//indicesV->push_back(face);
						if (i % 2 == 0)
							v0 = index;
						else
							v1 = index;
					}
				}
				break;
			case GL_QUAD_STRIP:
			case GL_LINES:
				//cout << "unimplemented primitive topology. skipping group (" << PGTopo << ")." << endl;
				break;
			default:
				//cout << "unknown primitive topology. skipping group (" << PGTopo << ")." << endl;
				break;
			}
		}


		newMesh->mFaces = indicesV->data();
		newMesh->mNumFaces = indicesV->size();
		newMesh->mPrimitiveTypes = aiPrimitiveType::aiPrimitiveType_TRIANGLE;


		if (indicesV->size() == 0)
		{
			//cout << "all primities skipped. skipping mesh." << endl;
		}
		else
		{
			//if (mesh->pAttributes->n > 1)
			if (mesh->pTransforms->n == 0)
			{
				auto node = new aiNode();
				node->mNumMeshes = 1;
				node->mTransformation = aiMatrix4x4();
				node->mNumChildren = 0;
				node->mParent = rootNode;
				node->mMeshes = new unsigned int[1]{ (unsigned int)meshes.size() };
				nodes.push_back(node);
			}
			else {
				for (int i = 0; i < mesh->pTransforms->n; i++)
				{
					auto transform = mesh->pTransforms->p[i].p;
					if (transform->nodeType == NODE_TRANSFORM || transform->nodeType == NODE_TRANSFORMSIMPLE)
					{
						auto matrix = transform->asTransfSimple()->pMatrixAbs;
						auto aiMatrix = aiMatrix4x4(
							matrix->m[0], matrix->m[1], matrix->m[2], matrix->m[3],
							matrix->m[4], matrix->m[5], matrix->m[6], matrix->m[7],
							matrix->m[8], matrix->m[9], matrix->m[10], matrix->m[11],
							matrix->m[12], matrix->m[13], matrix->m[14], matrix->m[15]);

						auto node = new aiNode();
						node->mNumMeshes = 1;
						node->mTransformation = aiMatrix;
						node->mNumChildren = 0;
						node->mParent = rootNode;
						node->mMeshes = new unsigned int[1]{ (unsigned int)meshes.size()};
						nodes.push_back(node);
					}
					else throw "dont understand bones";
					//if (auto v = dynamic_cast<bk3d::TransformSimple*>(transform.p)) {
					//}
				}
			}
			meshes.push_back(newMesh);
		}
	}
	pScene->mMeshes = vecToArray(meshes);
	pScene->mNumMeshes = meshes.size();

	if (nodes.size() == 0)
	{
		cout << "no meshes in scene" << endl;
	}
	aiNode** nn = vecToArray(nodes);

	rootNode->mChildren = nn;
	rootNode->mNumChildren = nodes.size();

	pScene->mRootNode = rootNode;
	pScene->mNumMaterials = 0;
	pScene->mNumAnimations = 0;
	pScene->mNumCameras = 0;
	pScene->mNumLights = 0;
	pScene->mNumTextures = 0;
	pScene->mFlags = 0;

	std::cout << pScene << endl;
	std::cout << pScene->mRootNode << endl;

	MakeVerboseFormatProcess().Execute(pScene);

	std::cout << "fixed up" << endl;
}


//// ------------------------------------------------------------------------------------------------
//aiNode *B3DImporter::ReadNODE(aiNode *parent){
//
//	string name = ReadString();
//	aiVector3D t = ReadVec3();
//	aiVector3D s = ReadVec3();
//	aiQuaternion r = ReadQuat();
//
//	aiMatrix4x4 trans, scale, rot;
//
//	aiMatrix4x4::Translation(t, trans);
//	aiMatrix4x4::Scaling(s, scale);
//	rot = aiMatrix4x4(r.GetMatrix());
//
//	aiMatrix4x4 tform = trans * rot * scale;
//
//	int nodeid = _nodes.size();
//
//	aiNode *node = new aiNode(name);
//	_nodes.push_back(node);
//
//	node->mParent = parent;
//	node->mTransformation = tform;
//
//	aiNodeAnim *nodeAnim = 0;
//	vector<unsigned> meshes;
//	vector<aiNode*> children;
//
//	while (ChunkSize()){
//		string t = ReadChunk();
//		if (t == "MESH"){
//			int n = _meshes.size();
//			ReadMESH();
//			for (int i = n; i<(int) _meshes.size(); ++i){
//				meshes.push_back(i);
//			}
//		}
//		else if (t == "BONE"){
//			ReadBONE(nodeid);
//		}
//		else if (t == "ANIM"){
//			ReadANIM();
//		}
//		else if (t == "KEYS"){
//			if (!nodeAnim){
//				nodeAnim = new aiNodeAnim;
//				_nodeAnims.push_back(nodeAnim);
//				nodeAnim->mNodeName = node->mName;
//			}
//			ReadKEYS(nodeAnim);
//		}
//		else if (t == "NODE"){
//			aiNode *child = ReadNODE(node);
//			children.push_back(child);
//		}
//		ExitChunk();
//	}
//
//	node->mNumMeshes = meshes.size();
//	node->mMeshes = to_array(meshes);
//
//	node->mNumChildren = children.size();
//	node->mChildren = to_array(children);
//
//	return node;
//}

//// ------------------------------------------------------------------------------------------------
//void B3DImporter::ReadBB3D(aiScene *scene){
//
//	_textures.clear();
//	_materials.clear();
//
//	_vertices.clear();
//	_meshes.clear();
//
//	_nodes.clear();
//	_nodeAnims.clear();
//	_animations.clear();
//
//	string t = ReadChunk();
//	if (t == "BB3D"){
//		int version = ReadInt();
//
//		if (!DefaultLogger::isNullLogger()) {
//			char dmp[128];
//			sprintf(dmp, "B3D file format version: %i", version);
//			DefaultLogger::get()->info(dmp);
//		}
//
//		while (ChunkSize()){
//			string t = ReadChunk();
//			if (t == "TEXS"){
//				ReadTEXS();
//			}
//			else if (t == "BRUS"){
//				ReadBRUS();
//			}
//			else if (t == "NODE"){
//				ReadNODE(0);
//			}
//			ExitChunk();
//		}
//	}
//	ExitChunk();
//
//	if (!_nodes.size()) Fail("No nodes");
//
//	if (!_meshes.size()) Fail("No meshes");
//
//	//Fix nodes/meshes/bones
//	for (size_t i = 0; i<_nodes.size(); ++i){
//		aiNode *node = _nodes[i];
//
//		for (size_t j = 0; j<node->mNumMeshes; ++j){
//			aiMesh *mesh = _meshes[node->mMeshes[j]];
//
//			int n_tris = mesh->mNumFaces;
//			int n_verts = mesh->mNumVertices = n_tris * 3;
//
//			aiVector3D *mv = mesh->mVertices = new aiVector3D[n_verts], *mn = 0, *mc = 0;
//			if (_vflags & 1) mn = mesh->mNormals = new aiVector3D[n_verts];
//			if (_tcsets) mc = mesh->mTextureCoords[0] = new aiVector3D[n_verts];
//
//			aiFace *face = mesh->mFaces;
//
//			vector< vector<aiVertexWeight> > vweights(_nodes.size());
//
//			for (int i = 0; i<n_verts; i += 3){
//				for (int j = 0; j<3; ++j){
//					Vertex &v = _vertices[face->mIndices[j]];
//
//					*mv++ = v.vertex;
//					if (mn) *mn++ = v.normal;
//					if (mc) *mc++ = v.texcoords;
//
//					face->mIndices[j] = i + j;
//
//					for (int k = 0; k<4; ++k){
//						if (!v.weights[k]) break;
//
//						int bone = v.bones[k];
//						float weight = v.weights[k];
//
//						vweights[bone].push_back(aiVertexWeight(i + j, weight));
//					}
//				}
//				++face;
//			}
//
//			vector<aiBone*> bones;
//			for (size_t i = 0; i<vweights.size(); ++i){
//				vector<aiVertexWeight> &weights = vweights[i];
//				if (!weights.size()) continue;
//
//				aiBone *bone = new aiBone;
//				bones.push_back(bone);
//
//				aiNode *bnode = _nodes[i];
//
//				bone->mName = bnode->mName;
//				bone->mNumWeights = weights.size();
//				bone->mWeights = to_array(weights);
//
//				aiMatrix4x4 mat = bnode->mTransformation;
//				while (bnode->mParent){
//					bnode = bnode->mParent;
//					mat = bnode->mTransformation * mat;
//				}
//				bone->mOffsetMatrix = mat.Inverse();
//			}
//			mesh->mNumBones = bones.size();
//			mesh->mBones = to_array(bones);
//		}
//	}
//
//	//nodes
//	scene->mRootNode = _nodes[0];
//
//	//material
//	if (!_materials.size()){
//		_materials.push_back(new aiMaterial);
//	}
//	scene->mNumMaterials = _materials.size();
//	scene->mMaterials = to_array(_materials);
//
//	//meshes
//	scene->mNumMeshes = _meshes.size();
//	scene->mMeshes = to_array(_meshes);
//
//	//animations
//	if (_animations.size() == 1 && _nodeAnims.size()){
//
//		aiAnimation *anim = _animations.back();
//		anim->mNumChannels = _nodeAnims.size();
//		anim->mChannels = to_array(_nodeAnims);
//
//		scene->mNumAnimations = _animations.size();
//		scene->mAnimations = to_array(_animations);
//	}
//
//	// convert to RH
//	MakeLeftHandedProcess makeleft;
//	makeleft.Execute(scene);
//
//	FlipWindingOrderProcess flip;
//	flip.Execute(scene);
//}
//
