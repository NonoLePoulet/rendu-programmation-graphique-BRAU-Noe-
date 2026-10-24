//-----------------------------------------------------------------------------
// Scène de ferme avec forêt environnante - OpenGL 3.3 core profile
//
// Décor :
//   - Ferme + forêt environnante   (farm+forest.glb, scène combinée)
//   - Trois barils devant la grange(barrel.obj)
//   - Deux cochons dans la cour    (Pig.obj)
//   - Un lapin dans le pré         (bunny.obj)
//   - Deux lampadaires : devant et derrière la grange (lampPost.obj)
//
// Éclairage :
//   - Lumière directionnelle (soleil couchant, crépuscule sombre)
//   - Lampadaire (point light chaude)
//   - Spot-light torche (touche F pour on/off, éteinte au départ)
//
// Contrôles :
//   WASD         - déplacement caméra FPS
//   Shift        - accélérer (sprint)
//   Espace / Z   - monter
//   Ctrl / X     - descendre
//   Molette      - zoom FOV
//   F1           - wireframe
//   F            - torche on/off
//   Echap        - quitter
//
// Mode démo :
//   ./Lighting_Farm demo   - la caméra fait automatiquement le tour de la
//                            ferme en boucle (souris/clavier de déplacement
//                            désactivés ; F, F1 et Echap restent actifs)
//-----------------------------------------------------------------------------
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <map>

#define GLEW_STATIC
#include "GL/glew.h"
#include "GLFW/glfw3.h"
#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"

#include "ShaderProgram.h"
#include "Texture2D.h"
#include "Camera.h"
#include "Mesh.h"

#define TINYGLTF_IMPLEMENTATION
// #define STB_IMAGE_IMPLEMENTATION // Commenté pour éviter le conflit de définitions multiples
#define STB_IMAGE_WRITE_IMPLEMENTATION
// #define TINYGLTF_NOEXCEPTION 
#include "tiny_gltf.h"

// Note : pas de "using namespace tinygltf;" pour éviter le conflit avec notre classe Mesh

// ---------------------------------------------------------------------------
// Variables Globales
// ---------------------------------------------------------------------------
const char* APP_TITLE = "OpenGL - Scène de Ferme";
int gWindowWidth  = 1280;
int gWindowHeight = 720;
GLFWwindow* gWindow  = NULL;
bool gWireframe      = false;
bool gFlashlightOn   = false; // torche éteinte au départ pour mettre en valeur le lampadaire

// Ciel crépusculaire (bleu-violet sombre)
glm::vec4 gClearColor(0.10f, 0.07f, 0.16f, 1.0f);

// Caméra au sol, face à l'entrée de la grange.
// Le portail est sur la face +X de la grange (x ≈ -1, z ≈ 1.3) ;
// on se place devant, côté cour, et on regarde vers -X (yaw = -90°).
// Caméra FPS avec en plus la possibilité de la placer directement
// (position + point visé), utilisée par le mode démo.
class DemoCamera : public FPSCamera
{
public:
	DemoCamera(glm::vec3 position, float yaw, float pitch) : FPSCamera(position, yaw, pitch) {}

	void lookFromTo(const glm::vec3& position, const glm::vec3& target)
	{
		glm::vec3 dir = glm::normalize(target - position);
		mPosition = position;
		mYaw      = atan2f(dir.x, dir.z);
		mPitch    = asinf(glm::clamp(dir.y, -1.0f, 1.0f));
		rotate(0.0f, 0.0f); // recalcule les vecteurs look/right/up
	}
};

DemoCamera fpsCamera(glm::vec3(13.0f, 2.8f, 1.3f), -glm::half_pi<float>(), glm::radians(-5.0f));

// ---------------------------------------------------------------------------
// Mode démo : tour automatique de la ferme
// Trajet en boucle défini par des points clés (position caméra + point visé),
// interpolés avec une spline de Catmull-Rom. Les arbres commencent à ~15
// unités du centre et montent jusqu'à ~28 : le tour se fait donc au-dessus
// de la cime (Y >= 30), avec montée/descente à l'intérieur de la clairière.
// ---------------------------------------------------------------------------
bool gDemoMode = false;

struct DemoKey { glm::vec3 pos; glm::vec3 target; };

const DemoKey DEMO_KEYS[] = {
	{ glm::vec3( 13.0f,  2.8f,   1.3f), glm::vec3( -1.0f, 3.0f,  1.3f) }, // devant l'entrée de la grange
	{ glm::vec3( 13.0f,  2.8f,   1.3f), glm::vec3( -1.0f, 3.0f,  1.3f) }, // (pause)
	{ glm::vec3(  9.0f,  6.0f,  -8.0f), glm::vec3(  0.0f, 3.0f,  0.0f) }, // on s'écarte en montant
	{ glm::vec3( 10.0f, 30.0f, -12.0f), glm::vec3(  0.0f, 3.0f,  0.0f) }, // montée au-dessus des arbres
	{ glm::vec3(  0.0f, 32.0f, -28.0f), glm::vec3(  0.0f, 3.0f,  0.0f) }, // tour : nord
	{ glm::vec3(-28.0f, 34.0f,   0.0f), glm::vec3(  0.0f, 3.0f,  0.0f) }, // tour : ouest
	{ glm::vec3(  0.0f, 32.0f,  28.0f), glm::vec3(  0.0f, 3.0f,  0.0f) }, // tour : sud
	{ glm::vec3( 19.0f, 32.0f,  19.0f), glm::vec3(  0.0f, 3.0f,  0.0f) }, // tour : sud-est
	{ glm::vec3( 13.0f, 30.0f,  10.0f), glm::vec3(  0.0f, 3.0f,  0.0f) }, // début de la descente
	{ glm::vec3( 11.0f,  6.0f,   5.0f), glm::vec3( -1.0f, 3.0f,  1.3f) }, // retour devant la grange
};
const int   DEMO_NUM_KEYS   = sizeof(DEMO_KEYS) / sizeof(DEMO_KEYS[0]);
const float DEMO_SPEED      = 7.0f;  // unités / seconde le long du trajet
const float DEMO_MIN_SEGMENT = 3.0f; // durée mini d'un segment (sert de pause)

glm::vec3 catmullRom(const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& p2, const glm::vec3& p3, float t)
{
	float t2 = t * t, t3 = t2 * t;
	return 0.5f * ((2.0f * p1) + (-p0 + p2) * t
	             + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2
	             + (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
}

// Place la caméra sur le trajet de démo au temps 'time' (en secondes)
void updateDemoCamera(double time)
{
	// Durée de chaque segment proportionnelle à sa longueur
	static std::vector<float> durations;
	static float totalDuration = 0.0f;
	if (durations.empty()) {
		for (int i = 0; i < DEMO_NUM_KEYS; ++i) {
			const DemoKey& a = DEMO_KEYS[i];
			const DemoKey& b = DEMO_KEYS[(i + 1) % DEMO_NUM_KEYS];
			float d = glm::max(glm::length(b.pos - a.pos) / DEMO_SPEED, DEMO_MIN_SEGMENT);
			durations.push_back(d);
			totalDuration += d;
		}
	}

	float t = fmodf((float)time, totalDuration);
	int seg = 0;
	while (seg < DEMO_NUM_KEYS - 1 && t > durations[seg]) { t -= durations[seg]; ++seg; }
	float u = t / durations[seg];
	u = u * u * (3.0f - 2.0f * u); // départ/arrivée en douceur sur chaque segment

	auto key = [](int i) -> const DemoKey& { return DEMO_KEYS[(i + DEMO_NUM_KEYS) % DEMO_NUM_KEYS]; };
	glm::vec3 pos    = catmullRom(key(seg - 1).pos,    key(seg).pos,    key(seg + 1).pos,    key(seg + 2).pos,    u);
	glm::vec3 target = catmullRom(key(seg - 1).target, key(seg).target, key(seg + 1).target, key(seg + 2).target, u);

	fpsCamera.lookFromTo(pos, target);
}
const double ZOOM_SENSITIVITY  = -3.0;
const float  MOVE_SPEED        = 8.0f;  // Vitesse de marche au sol (Shift pour accélérer)
const float  MOUSE_SENSITIVITY = 0.005f;

// ---------------------------------------------------------------------------
// Structure et gestionnaire pour les modèles glTF (Ferme et Forêt)
// ---------------------------------------------------------------------------
struct GLTFPrimitive {
	GLuint vao;
	int indexCount;
	int indexType;
	size_t indexOffset;
	int vertexCount;
	GLuint texture;
};

struct GLTFMesh {
	std::vector<GLTFPrimitive> primitives;
};

struct GLTFNodeInstance {
	int meshIndex;
	glm::mat4 transform;
};

struct GLTFModel {
	std::vector<GLTFMesh> meshes;
	std::vector<GLTFNodeInstance> nodes;
	std::map<int, GLuint> vbos;
	std::map<int, GLuint> textures; // Cache pour réutiliser les textures déjà créées
	bool loaded = false;

	void traverseNode(const tinygltf::Model& model, int nodeIdx, const glm::mat4& parentTransform) {
		const tinygltf::Node& node = model.nodes[nodeIdx];
		glm::mat4 localTransform(1.0f);

		if (node.matrix.size() == 16) {
			for (int c = 0; c < 4; ++c)
				for (int r = 0; r < 4; ++r)
					localTransform[c][r] = (float)node.matrix[c * 4 + r];
		} else {
			glm::mat4 t(1.0f), r(1.0f), s(1.0f);
			if (node.translation.size() == 3) {
				t = glm::translate(glm::mat4(1.0f), glm::vec3((float)node.translation[0], (float)node.translation[1], (float)node.translation[2]));
			}
			if (node.rotation.size() == 4) {
				float rx = (float)node.rotation[0], ry = (float)node.rotation[1];
				float rz = (float)node.rotation[2], rw = (float)node.rotation[3];
				float xx = rx*rx, yy = ry*ry, zz = rz*rz;
				float xy = rx*ry, xz = rx*rz, yz = ry*rz;
				float wx = rw*rx, wy = rw*ry, wz = rw*rz;
				r[0][0] = 1.0f - 2.0f * (yy + zz);
				r[0][1] = 2.0f * (xy + wz);
				r[0][2] = 2.0f * (xz - wy);
				r[1][0] = 2.0f * (xy - wz);
				r[1][1] = 1.0f - 2.0f * (xx + zz);
				r[1][2] = 2.0f * (yz + wx);
				r[2][0] = 2.0f * (xz + wy);
				r[2][1] = 2.0f * (yz - wx);
				r[2][2] = 1.0f - 2.0f * (xx + yy);
			}
			if (node.scale.size() == 3) {
				s = glm::scale(glm::mat4(1.0f), glm::vec3((float)node.scale[0], (float)node.scale[1], (float)node.scale[2]));
			}
			localTransform = t * r * s;
		}

		glm::mat4 globalTransform = parentTransform * localTransform;

		if (node.mesh >= 0) {
			nodes.push_back({node.mesh, globalTransform});
		}

		for (size_t i = 0; i < node.children.size(); i++) {
			traverseNode(model, node.children[i], globalTransform);
		}
	}

	bool load(const std::string& filename) {
		tinygltf::Model model;
		tinygltf::TinyGLTF loader;
		std::string err, warn;
		
		bool ret = loader.LoadBinaryFromFile(&model, &err, &warn, filename);
		if (!warn.empty()) std::cout << "GLTF Warn (" << filename << "): " << warn << std::endl;
		if (!err.empty()) std::cerr << "GLTF Err (" << filename << "): " << err << std::endl;
		if (!ret) {
			std::cerr << "Échec du chargement du fichier glTF: " << filename << std::endl;
			return false;
		}

		// 1. Charger les buffers en VRAM
		for (size_t i = 0; i < model.bufferViews.size(); ++i) {
			const tinygltf::BufferView &bufferView = model.bufferViews[i];
			int target = bufferView.target == 0 ? GL_ARRAY_BUFFER : bufferView.target;
			GLuint vbo;
			glGenBuffers(1, &vbo);
			vbos[i] = vbo;
			glBindBuffer(target, vbo);
			glBufferData(target, bufferView.byteLength,
						 &model.buffers[bufferView.buffer].data.at(0) + bufferView.byteOffset,
						 GL_STATIC_DRAW);
		}

		// 2. Extraire la géométrie (VAO)
		meshes.resize(model.meshes.size());
		for (size_t m = 0; m < model.meshes.size(); ++m) {
			const auto& mesh = model.meshes[m];
			for (const auto& primitive : mesh.primitives) {
				GLTFPrimitive primObj = {0, 0, 0, 0, 0, 0};
				glGenVertexArrays(1, &primObj.vao);
				glBindVertexArray(primObj.vao);

				if (primitive.indices >= 0) {
					const tinygltf::Accessor &accessor = model.accessors[primitive.indices];
					glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, vbos[accessor.bufferView]);
					primObj.indexCount = accessor.count;
					primObj.indexType = accessor.componentType;
					primObj.indexOffset = accessor.byteOffset;
				}

				for (auto& attrib : primitive.attributes) {
					const tinygltf::Accessor &accessor = model.accessors[attrib.second];
					glBindBuffer(GL_ARRAY_BUFFER, vbos[accessor.bufferView]);

					int size = tinygltf::GetNumComponentsInType(accessor.type);
					int location = -1;
					if (attrib.first == "POSITION") location = 0;
					else if (attrib.first == "NORMAL") location = 1;
					else if (attrib.first == "TEXCOORD_0") location = 2;

					if (location >= 0) {
						glEnableVertexAttribArray(location);
						const tinygltf::BufferView &bufferView = model.bufferViews[accessor.bufferView];
						int stride = accessor.ByteStride(bufferView);
						glVertexAttribPointer(location, size, accessor.componentType,
											  accessor.normalized ? GL_TRUE : GL_FALSE,
											  stride, (void*)accessor.byteOffset);
					}
					if (attrib.first == "POSITION" && primitive.indices < 0) {
						primObj.vertexCount = accessor.count;
					}
				}
				
				if (primitive.material >= 0) {
					const tinygltf::Material &mat = model.materials[primitive.material];
					if (mat.pbrMetallicRoughness.baseColorTexture.index >= 0) {
						int texIdx = mat.pbrMetallicRoughness.baseColorTexture.index;
						int imageIdx = model.textures[texIdx].source;
						if (imageIdx >= 0) {
							if (textures.find(imageIdx) != textures.end()) {
								primObj.texture = textures[imageIdx];
							} else {
								const tinygltf::Image &image = model.images[imageIdx];
								GLuint tex = 0;
								glGenTextures(1, &tex);
								glBindTexture(GL_TEXTURE_2D, tex);
								glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
								glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
								glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
								glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
								GLenum format = (image.component == 3) ? GL_RGB : GL_RGBA;
								if (image.component == 1) format = GL_RED;
								glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
								glTexImage2D(GL_TEXTURE_2D, 0, format, image.width, image.height, 0,
											 format, GL_UNSIGNED_BYTE, &image.image[0]);
								glGenerateMipmap(GL_TEXTURE_2D);
								textures[imageIdx] = tex;
								primObj.texture = tex;
							}
						}
					}
				}
				meshes[m].primitives.push_back(primObj);
				glBindVertexArray(0);
			}
		}

		// 3. Lire la scène pour récupérer l'échelle et la position
		int sceneIdx = model.defaultScene >= 0 ? model.defaultScene : 0;
		if (sceneIdx >= 0 && sceneIdx < (int)model.scenes.size()) {
			const tinygltf::Scene& scene = model.scenes[sceneIdx];
			for (size_t i = 0; i < scene.nodes.size(); i++) {
				traverseNode(model, scene.nodes[i], glm::mat4(1.0f));
			}
		}

		loaded = true;
		return true;
	}

	void draw(ShaderProgram& shader, const glm::mat4& rootModel, Texture2D* fallbackTexture = nullptr) {
		for (const auto& nodeInst : nodes) {
			glm::mat4 nodeModel = rootModel * nodeInst.transform;
			shader.setUniform("model", nodeModel);

			const GLTFMesh& gMesh = meshes[nodeInst.meshIndex];
			for (const auto& prim : gMesh.primitives) {
				glActiveTexture(GL_TEXTURE0);
				if (prim.texture != 0) {
					glBindTexture(GL_TEXTURE_2D, prim.texture);
				} else if (fallbackTexture != nullptr) {
					fallbackTexture->bind(0);
				}
				
				glBindVertexArray(prim.vao);
				if (prim.indexCount > 0) {
					glDrawElements(GL_TRIANGLES, prim.indexCount, prim.indexType, (void*)prim.indexOffset);
				} else {
					glDrawArrays(GL_TRIANGLES, 0, prim.vertexCount);
				}
				glBindVertexArray(0);
			}
		}
	}
};

// ---------------------------------------------------------------------------
// Function prototypes
// ---------------------------------------------------------------------------
void glfw_onKey(GLFWwindow* window, int key, int scancode, int action, int mode);
void glfw_onFramebufferSize(GLFWwindow* window, int width, int height);
void glfw_onMouseScroll(GLFWwindow* window, double deltaX, double deltaY);
void update(double elapsedTime);
void showFPS(GLFWwindow* window);
bool initOpenGL();


//-----------------------------------------------------------------------------
// Main Application Entry Point
//-----------------------------------------------------------------------------
int main(int argc, char** argv)
{
	for (int i = 1; i < argc; ++i) {
		std::string arg = argv[i];
		if (arg == "demo" || arg == "--demo") {
			gDemoMode = true;
		} else {
			std::cerr << "Argument inconnu : " << arg << std::endl
			          << "Usage : " << argv[0] << " [demo]" << std::endl;
			return -1;
		}
	}
	if (gDemoMode)
		std::cout << "Mode démo : tour automatique de la ferme (Echap pour quitter)" << std::endl;

	if (!initOpenGL())
	{
		std::cerr << "GLFW initialization failed" << std::endl;
		return -1;
	}

	// Initialiser les vecteurs d'orientation de la caméra avec le pitch plongeant
	fpsCamera.rotate(0.0f, 0.0f);

	// -------------------------------------------------------------------------
	// Shader : on réutilise le shader dir+point+spot
	// -------------------------------------------------------------------------
	ShaderProgram lightingShader;
	lightingShader.loadShaders("shaders/lighting_dir_point_spot.vert",
	                           "shaders/lighting_dir_point_spot.frag");

	// -------------------------------------------------------------------------
	// Chargement des meshes et textures
	// -------------------------------------------------------------------------
	const int numModels = 9;
	Mesh      mesh[numModels];
	Texture2D texture[numModels];

	// -------------------------------------------------------------------------
	// Chargement des modèles GLB avec tinygltf
	// -------------------------------------------------------------------------
	GLTFModel sceneModel; // ferme + forêt dans un seul fichier

	std::cout << "Chargement de la scène (farm+forest.glb)..." << std::endl;
	if (!sceneModel.load("models/farm+forest.glb")) {
		std::cerr << "Impossible de charger models/farm+forest.glb" << std::endl;
	}

	// Meshes (l'index 0 est gardé vide : la ferme fait partie de farm+forest.glb)
	mesh[1].loadOBJ("models/barrel.obj");      // baril 1
	mesh[2].loadOBJ("models/barrel.obj");      // baril 2
	mesh[3].loadOBJ("models/barrel.obj");      // baril 3 (empilé)
	mesh[4].loadOBJ("models/Pig.obj");         // cochon
	mesh[5].loadOBJ("models/Chicken.obj");     // poulet
	mesh[6].loadOBJ("models/bunny.obj");       // lapin
	mesh[7].loadOBJ("models/lampPost.obj");    // lampadaire (devant)
	mesh[8].loadOBJ("models/lampPost.obj");    // lampadaire (derrière la grange)

	// Textures
	texture[0].loadTexture("textures/farm_green.png",        true); // Texture fallback pour la ferme
	texture[1].loadTexture("textures/barrel_diffuse.png",    true); // baril 1
	texture[2].loadTexture("textures/barrel_diffuse.png",    true); // baril 2
	texture[3].loadTexture("textures/barrel_diffuse.png",    true); // baril 3
	texture[4].loadTexture("textures/pig_pink.png",          true); // cochon (rose)
	texture[5].loadTexture("textures/chicken.png",           true); // poulet
	texture[6].loadTexture("textures/bunny_diffuse.jpg",     true); // lapin
	texture[7].loadTexture("textures/lamp_post_diffuse.png", true); // lampadaire (devant)
	texture[8].loadTexture("textures/lamp_post_diffuse.png", true); // lampadaire (derrière)

	// -------------------------------------------------------------------------
	// Sol de référence et élévation de la ferme
	// -------------------------------------------------------------------------
	const float FARM_FLOOR_Y = 1.0f; // Ferme montée d'une unité au-dessus du sol pour une assise nette

	// -------------------------------------------------------------------------
	// Transformation de la scène farm+forest.glb
	// Dans le fichier, le socle de la ferme est centré en (X ≈ 7.5, Z ≈ 79.4)
	// et sa surface est à Y ≈ -27.23. On décale toute la scène pour que la
	// ferme soit à l'origine et que son sol arrive à FARM_FLOOR_Y.
	// -------------------------------------------------------------------------
	const float SCENE_SCALE  = 1.0f;
	const float FARM_CENTER_X = 7.52f;
	const float FARM_CENTER_Z = 79.43f;
	const float FARM_GROUND_Y = -27.23f;
	glm::vec3 scenePos(-FARM_CENTER_X * SCENE_SCALE,
	                   FARM_FLOOR_Y - FARM_GROUND_Y * SCENE_SCALE,
	                   -FARM_CENTER_Z * SCENE_SCALE);
	glm::vec3 sceneScale(SCENE_SCALE);
	float     sceneRotY = 0.0f;

	// -------------------------------------------------------------------------
	// Positions des modèles dans la scène (La ferme reste à l'origine)
	// -------------------------------------------------------------------------
	glm::vec3 modelPos[] = {
		glm::vec3(  0.0f,  FARM_FLOOR_Y,         0.0f),  // (inutilisé : ferme dans farm+forest.glb)
		glm::vec3(  6.5f,  FARM_FLOOR_Y,        -4.2f),  // baril 1
		glm::vec3(  7.5f,  FARM_FLOOR_Y,        -4.2f),  // baril 2
		glm::vec3(  7.0f,  FARM_FLOOR_Y + 2.68f,-4.2f),  // baril 3 (empilé)
		glm::vec3( -4.0f,  FARM_FLOOR_Y,         3.0f),  // cochon
		glm::vec3( -2.5f,  FARM_FLOOR_Y + 0.512f,5.0f),  // poulet 
		glm::vec3(  5.0f,  FARM_FLOOR_Y,         4.0f),  // lapin 
		glm::vec3(  4.5f,  FARM_FLOOR_Y,        -0.3f),  // lampadaire (entre la vache et la grange)
		glm::vec3(-12.8f,  FARM_FLOOR_Y,         0.5f),  // lampadaire 2 (entre l'arrière de la grange et la clôture)
	};

	// -------------------------------------------------------------------------
	// Échelles des modèles
	// -------------------------------------------------------------------------
	glm::vec3 modelScale[] = {
		glm::vec3( 1.0f,  1.0f,  1.0f),  // ferme (taille naturelle)
		glm::vec3( 0.8f,  0.8f,  0.8f),  // baril 1
		glm::vec3( 0.8f,  0.8f,  0.8f),  // baril 2
		glm::vec3( 0.8f,  0.8f,  0.8f),  // baril 3
		glm::vec3( 0.3f,  0.3f,  0.3f),  // cochon
		glm::vec3( 2.0f,  2.0f,  2.0f),  // poulet 
		glm::vec3( 0.45f, 0.45f, 0.45f), // lapin
		glm::vec3( 1.0f,  1.0f,  1.0f),  // lampadaire
		glm::vec3( 1.0f,  1.0f,  1.0f),  // lampadaire 2
	};

	// -------------------------------------------------------------------------
	// Rotations Y des modèles (en degrés)
	// -------------------------------------------------------------------------
	float modelRotY[] = {
		  0.0f,   // ferme
		  0.0f,   // baril 1
		 15.0f,   // baril 2 
		  0.0f,   // baril 3
		-30.0f,   // cochon 
		 45.0f,   // poulet
		  0.0f,   // lapin
		  0.0f,   // lampadaire
		  0.0f,   // lampadaire 2
	};

	// Position des lampes des lampadaires (en haut du poteau, ~4 unités)
	glm::vec3 lampLightPos (  4.5f, FARM_FLOOR_Y + 4.0f, -0.3f);
	glm::vec3 lampLightPos2(-12.8f, FARM_FLOOR_Y + 4.0f,  0.5f);

	double lastTime  = glfwGetTime();
	double demoStart = lastTime;

	// =========================================================================
	// Boucle de rendu
	// =========================================================================
	while (!glfwWindowShouldClose(gWindow))
	{
		showFPS(gWindow);

		double currentTime = glfwGetTime();
		double deltaTime   = currentTime - lastTime;

		glfwPollEvents();
		if (gDemoMode)
			updateDemoCamera(currentTime - demoStart);
		else
			update(deltaTime);

		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		glm::mat4 model(1.0), view(1.0), projection(1.0);

		view       = fpsCamera.getViewMatrix();
		projection = glm::perspective(glm::radians(fpsCamera.getFOV()),
		                              (float)gWindowWidth / (float)gWindowHeight,
		                              0.1f, 1000.0f);

		lightingShader.use();

		lightingShader.setUniform("view",       view);
		lightingShader.setUniform("projection", projection);
		lightingShader.setUniform("viewPos",    fpsCamera.getPosition());

		// -- Soleil couchant : très bas sur l'horizon, faible, teinte orangé-violet --
		lightingShader.setUniform("sunLight.direction", glm::vec3(-0.9f, -0.15f, 0.3f));
		lightingShader.setUniform("sunLight.ambient",   glm::vec3(0.04f, 0.035f, 0.07f));
		lightingShader.setUniform("sunLight.diffuse",   glm::vec3(0.30f, 0.15f,  0.12f));
		lightingShader.setUniform("sunLight.specular",  glm::vec3(0.08f, 0.04f,  0.04f));

		// -- Lampadaire : lumière chaude et intense, portée ~30 unités --
		lightingShader.setUniform("pointLights[0].ambient",  glm::vec3(0.08f, 0.05f, 0.02f));
		lightingShader.setUniform("pointLights[0].diffuse",  glm::vec3(2.2f,  1.6f,  0.8f));
		lightingShader.setUniform("pointLights[0].specular", glm::vec3(1.5f,  1.2f,  0.7f));
		lightingShader.setUniform("pointLights[0].position", lampLightPos);
		lightingShader.setUniform("pointLights[0].constant", 1.0f);
		lightingShader.setUniform("pointLights[0].linear",   0.09f);
		lightingShader.setUniform("pointLights[0].exponent", 0.032f);

		// -- Lampadaire 2 (derrière la grange) : même lumière chaude --
		lightingShader.setUniform("pointLights[1].ambient",  glm::vec3(0.08f, 0.05f, 0.02f));
		lightingShader.setUniform("pointLights[1].diffuse",  glm::vec3(2.2f,  1.6f,  0.8f));
		lightingShader.setUniform("pointLights[1].specular", glm::vec3(1.5f,  1.2f,  0.7f));
		lightingShader.setUniform("pointLights[1].position", lampLightPos2);
		lightingShader.setUniform("pointLights[1].constant", 1.0f);
		lightingShader.setUniform("pointLights[1].linear",   0.09f);
		lightingShader.setUniform("pointLights[1].exponent", 0.032f);

		// -- Point light 3 éteinte (utilisation de .c_str() pour éviter les erreurs de compilation) --
		for (int i = 2; i <= 2; ++i) {
			std::string base = "pointLights[" + std::to_string(i) + "].";
			lightingShader.setUniform((base + "ambient").c_str(),  glm::vec3(0.0f));
			lightingShader.setUniform((base + "diffuse").c_str(),  glm::vec3(0.0f));
			lightingShader.setUniform((base + "specular").c_str(), glm::vec3(0.0f));
			lightingShader.setUniform((base + "position").c_str(), glm::vec3(0.0f));
			lightingShader.setUniform((base + "constant").c_str(), 1.0f);
			lightingShader.setUniform((base + "linear").c_str(),   0.0f);
			lightingShader.setUniform((base + "exponent").c_str(), 0.0f);
		}

		glm::vec3 spotlightPos = fpsCamera.getPosition();
		spotlightPos.y -= 0.3f;

		lightingShader.setUniform("spotLight.ambient",      glm::vec3(0.02f, 0.02f, 0.03f)); // appliqué même torche éteinte
		lightingShader.setUniform("spotLight.diffuse",      glm::vec3(0.9f, 0.9f, 0.9f));
		lightingShader.setUniform("spotLight.specular",     glm::vec3(1.0f, 1.0f, 1.0f));
		lightingShader.setUniform("spotLight.position",     spotlightPos);
		lightingShader.setUniform("spotLight.direction",    fpsCamera.getLook());
		lightingShader.setUniform("spotLight.cosInnerCone", glm::cos(glm::radians(12.5f)));
		lightingShader.setUniform("spotLight.cosOuterCone", glm::cos(glm::radians(17.5f)));
		lightingShader.setUniform("spotLight.constant",     1.0f);
		lightingShader.setUniform("spotLight.linear",       0.07f);
		lightingShader.setUniform("spotLight.exponent",     0.017f);
		lightingShader.setUniform("spotLight.on",           gFlashlightOn);

		// -- Rendu de tous les objets de la ferme --
		for (int i = 1; i < numModels; i++)
		{
			model = glm::translate(glm::mat4(1.0f), modelPos[i])
			      * glm::rotate(glm::mat4(1.0f),
			                    glm::radians(modelRotY[i]),
			                    glm::vec3(0.0f, 1.0f, 0.0f))
			      * glm::scale(glm::mat4(1.0f), modelScale[i]);

			lightingShader.setUniform("material.ambient",    glm::vec3(0.1f, 0.1f, 0.1f));
			lightingShader.setUniformSampler("material.diffuseMap", 0);
			lightingShader.setUniform("material.specular",   glm::vec3(0.4f, 0.4f, 0.4f));
			lightingShader.setUniform("material.shininess",  32.0f);

			lightingShader.setUniform("model", model);
			texture[i].bind(0);
			mesh[i].draw();
			texture[i].unbind(0);
		}

		// -- Rendu de la scène glTF ferme + forêt (farm+forest.glb) --
		if (sceneModel.loaded) {
			glm::mat4 sceneMat = glm::translate(glm::mat4(1.0f), scenePos)
			                   * glm::rotate(glm::mat4(1.0f),
			                                 glm::radians(sceneRotY),
			                                 glm::vec3(0.0f, 1.0f, 0.0f))
			                   * glm::scale(glm::mat4(1.0f), sceneScale);

			lightingShader.setUniform("material.ambient",    glm::vec3(0.18f, 0.18f, 0.18f));
			lightingShader.setUniformSampler("material.diffuseMap", 0);
			lightingShader.setUniform("material.specular",   glm::vec3(0.15f, 0.15f, 0.15f));
			lightingShader.setUniform("material.shininess",  16.0f);

			sceneModel.draw(lightingShader, sceneMat, &texture[0]);
		}

		glfwSwapBuffers(gWindow);
		lastTime = currentTime;
	}

	glfwTerminate();
	return 0;
}


//-----------------------------------------------------------------------------
// Initialize GLFW and OpenGL
//-----------------------------------------------------------------------------
bool initOpenGL()
{
	if (!glfwInit())
	{
		std::cerr << "GLFW initialization failed" << std::endl;
		return false;
	}

	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);

	gWindow = glfwCreateWindow(gWindowWidth, gWindowHeight, APP_TITLE, NULL, NULL);
	if (gWindow == NULL)
	{
		std::cerr << "Failed to create GLFW window" << std::endl;
		glfwTerminate();
		return false;
	}

	glfwMakeContextCurrent(gWindow);

	glewExperimental = GL_TRUE;
	if (glewInit() != GLEW_OK)
	{
		std::cerr << "Failed to initialize GLEW" << std::endl;
		return false;
	}

	glfwSetKeyCallback(gWindow,            glfw_onKey);
	glfwSetFramebufferSizeCallback(gWindow, glfw_onFramebufferSize);
	glfwSetScrollCallback(gWindow,         glfw_onMouseScroll);

	glfwSetInputMode(gWindow, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
	glfwSetCursorPos(gWindow, gWindowWidth / 2.0, gWindowHeight / 2.0);

	glClearColor(gClearColor.r, gClearColor.g, gClearColor.b, gClearColor.a);

	int w, h;
	glfwGetFramebufferSize(gWindow, &w, &h);
	glViewport(0, 0, w, h);

	glEnable(GL_DEPTH_TEST);

	return true;
}


//-----------------------------------------------------------------------------
// Is called whenever a key is pressed/released via GLFW
//-----------------------------------------------------------------------------
void glfw_onKey(GLFWwindow* window, int key, int scancode, int action, int mode)
{
	if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
		glfwSetWindowShouldClose(window, GL_TRUE);

	if (key == GLFW_KEY_F1 && action == GLFW_PRESS)
	{
		gWireframe = !gWireframe;
		glPolygonMode(GL_FRONT_AND_BACK, gWireframe ? GL_LINE : GL_FILL);
	}

	if (key == GLFW_KEY_F && action == GLFW_PRESS)
		gFlashlightOn = !gFlashlightOn;
}


//-----------------------------------------------------------------------------
// Is called when the window is resized
//-----------------------------------------------------------------------------
void glfw_onFramebufferSize(GLFWwindow* window, int width, int height)
{
	gWindowWidth  = width;
	gWindowHeight = height;

	int w, h;
	glfwGetFramebufferSize(gWindow, &w, &h);
	glViewport(0, 0, w, h);
}


//-----------------------------------------------------------------------------
// Called by GLFW when the mouse wheel is rotated
//-----------------------------------------------------------------------------
void glfw_onMouseScroll(GLFWwindow* window, double deltaX, double deltaY)
{
	if (gDemoMode) return; // pas de zoom pendant la démo
	double fov = fpsCamera.getFOV() + deltaY * ZOOM_SENSITIVITY;
	fov = glm::clamp(fov, 1.0, 120.0);
	fpsCamera.setFOV((float)fov);
}


//-----------------------------------------------------------------------------
// Update – camera FPS movement every frame
//-----------------------------------------------------------------------------
void update(double elapsedTime)
{
	double mouseX, mouseY;
	glfwGetCursorPos(gWindow, &mouseX, &mouseY);

	fpsCamera.rotate(
		(float)(gWindowWidth  / 2.0 - mouseX) * MOUSE_SENSITIVITY,
		(float)(gWindowHeight / 2.0 - mouseY) * MOUSE_SENSITIVITY);

	glfwSetCursorPos(gWindow, gWindowWidth / 2.0, gWindowHeight / 2.0);

	// Vitesse rapide si Shift est maintenu
	float speed = MOVE_SPEED;
	if (glfwGetKey(gWindow, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
	    glfwGetKey(gWindow, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS)
	{
		speed *= 3.0f;
	}

	if (glfwGetKey(gWindow, GLFW_KEY_W) == GLFW_PRESS)
		fpsCamera.move(speed * (float)elapsedTime *  fpsCamera.getLook());
	else if (glfwGetKey(gWindow, GLFW_KEY_S) == GLFW_PRESS)
		fpsCamera.move(speed * (float)elapsedTime * -fpsCamera.getLook());

	if (glfwGetKey(gWindow, GLFW_KEY_A) == GLFW_PRESS)
		fpsCamera.move(speed * (float)elapsedTime * -fpsCamera.getRight());
	else if (glfwGetKey(gWindow, GLFW_KEY_D) == GLFW_PRESS)
		fpsCamera.move(speed * (float)elapsedTime *  fpsCamera.getRight());

	// Monter (Espace ou Z) / Descendre (Ctrl ou X)
	if (glfwGetKey(gWindow, GLFW_KEY_Z) == GLFW_PRESS || glfwGetKey(gWindow, GLFW_KEY_SPACE) == GLFW_PRESS)
		fpsCamera.move(speed * (float)elapsedTime *  glm::vec3(0.0f, 1.0f, 0.0f));
	else if (glfwGetKey(gWindow, GLFW_KEY_X) == GLFW_PRESS || glfwGetKey(gWindow, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS)
		fpsCamera.move(speed * (float)elapsedTime * -glm::vec3(0.0f, 1.0f, 0.0f));
}


//-----------------------------------------------------------------------------
// Affiche les FPS et le temps par frame dans la barre de titre
//-----------------------------------------------------------------------------
void showFPS(GLFWwindow* window)
{
	static double previousSeconds = 0.0;
	static int    frameCount      = 0;
	double currentSeconds = glfwGetTime();
	double elapsedSeconds = currentSeconds - previousSeconds;

	if (elapsedSeconds > 0.25)
	{
		previousSeconds   = currentSeconds;
		double fps        = (double)frameCount / elapsedSeconds;
		double msPerFrame = 1000.0 / fps;

		std::ostringstream outs;
		outs.precision(3);
		outs << std::fixed
		     << APP_TITLE << "    "
		     << "FPS: "        << fps        << "    "
		     << "Frame Time: " << msPerFrame << " (ms)";
		glfwSetWindowTitle(window, outs.str().c_str());

		frameCount = 0;
	}

	frameCount++;
}