#include <exception>
#include <filesystem>
#include <format>

#pragma warning(push)
#pragma warning(disable: 4307)
#include <spdlog/spdlog.h>
#pragma warning(pop)

#include "core/YamlSerializer.h"
#include "core/ClipperUtils.h"

#include "Document.h"

extern spdlog::logger* gLogger;


namespace floored
{
	using namespace std;

	Document* Document::msInstance = nullptr;

	Document::Document()
	{
	}

	Document::~Document()
	{
	}

	Document* Document::instance()
	{
		if (!msInstance)
		{
			msInstance = new Document();
		}

		return msInstance;
	}

	bool Document::isActive() const
	{
		return mWorld != nullptr;
	}

	shared_ptr<bw::core::World> Document::getWorld()
	{
		return mWorld;
	}

	std::vector<bw::core::Clipper2Polygon> const& Document::getPolygons()
	{
		return mPolygons;
	}

	expr::PSLG const& Document::getPSLG() const
	{
		return mPSLG;
	}

	vector<expr::Cycle> const& Document::getCycles() const
	{
		return mCycles;
	}

	vector<expr::PolygonNode> const& Document::getHierarchy() const
	{
		return mHierarchy;
	}

	std::vector<expr::Face> const& Document::getFaces() const
	{
		return mFaces;
	}

	vector<expr::FaceTriangle> const& Document::getFaceTriangles() const
	{
		return mFaceTriangles;
	}

	void Document::buildExpr()
	{
		if (!mWorld)
		{
			return;
		}

		auto primitives = mWorld->getPrimitives();

		mPolygons.clear();
		
		for (auto prim : primitives)
		{
			auto polygons = bw::core::ClipperUtils::convertPrimitiveToClipperPolygons(prim);

			mPolygons.reserve(mPolygons.size() + distance(polygons.begin(), polygons.end()));
			mPolygons.insert(mPolygons.end(), polygons.begin(), polygons.end());
		}

		mPSLG = expr::BuildPSLG(mPolygons, primitives);

		mCycles = expr::ExtractMinimalCycles(mPSLG);
		mHierarchy = expr::BuildPolygonHierarchy(mPSLG, mCycles);

 		mFaces = expr::BuildFaces(mHierarchy, mCycles);
		mFaces = expr::CalculateOwningPolygons(mFaces, mPolygons, mCycles, mPSLG, primitives);
		
		mFaceTriangles = expr::BuildFaceTriangles(mFaces, mCycles, mPSLG);
	}

	bool Document::openWorld(string const& filepath)
	{
		auto path = filesystem::path(filepath);
		auto ext = path.extension().string();
		transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

		if (ext == ".yaml")
		{
			auto ser = shared_ptr<bw::core::YamlSerializer>(bw::core::YamlSerializer::fromFile(filepath));

			try
			{
				ser->deserialize();
			}
			catch (exception& e)
			{
				gLogger->error(e.what());
				return false;
			}

			mWorld = make_shared<bw::core::World>(1.0f, -1.0f);

			auto workData = bw::core::SerializationWorkData{ 512.0f };

			if (mWorld->deserialize(ser, workData))
			{
				auto const& warnings = mWorld->getDeserializationWarnings();

				if (!warnings.empty())
				{
					for (auto const& warning : warnings)
					{
						gLogger->warn(warning);
					}
				}

				buildExpr();
				return true;
			}
			else
			{
				auto const& errors = mWorld->getDeserializationErrors();

				if (!errors.empty())
				{
					for (auto const& error : errors)
					{
						gLogger->error(error);
					}
				}

				return false;
			}
		}
		else
		{
			return false;
		}
	}

} // floored