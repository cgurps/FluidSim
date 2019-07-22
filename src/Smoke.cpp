#include "Smoke.h"
#include "GLUtils.h"

#include <string>
#include <sstream>
#include <fstream>
#include <cmath>

Smoke::~Smoke()
{
  GL_CHECK( glDeleteTextures(4, velocitiesTexture) );
  GL_CHECK( glDeleteTextures(4, density) );
  GL_CHECK( glDeleteTextures(1, &divergenceCurlTexture) );
  GL_CHECK( glDeleteTextures(2, pressureTexture) );
  GL_CHECK( glDeleteTextures(1, &emptyTexture) );
}

void Smoke::Init()
{
  GLint work_grp_cnt[3];
  GL_CHECK( glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT, 0, &work_grp_cnt[0]) );
  GL_CHECK( glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT, 1, &work_grp_cnt[1]) );
  GL_CHECK( glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT, 2, &work_grp_cnt[2]) );

  GLint work_grp_size[3];
  GL_CHECK( glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_SIZE, 0, &work_grp_size[0]) );
  GL_CHECK( glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_SIZE, 1, &work_grp_size[1]) );
  GL_CHECK( glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_SIZE, 2, &work_grp_size[2]) );

  GLint work_grp_inv;
  GL_CHECK( glGetIntegerv(GL_MAX_COMPUTE_WORK_GROUP_INVOCATIONS, &work_grp_inv) );
 
  printf("Max global (total) work group size x:%i y:%i z:%i\n",
    work_grp_cnt[0], work_grp_cnt[1], work_grp_cnt[2]);
  printf("Max local (in one shader) work group sizes x:%i y:%i z:%i\n",
    work_grp_size[0], work_grp_size[1], work_grp_size[2]);
  printf("Max local work group invocations %i\n", work_grp_inv);

  /********** Texture Initilization **********/
  auto f = [](int x, int y)
      {
        return std::make_tuple(0.0f, 0.0f,
                               0.0f, 0.0f);
      };

  //Velocities init function
  auto f1 = [this](int x, int y)
      {
        float xf = (float) x - 0.5f * (float) this->options->simWidth;
        float yf = (float) y - 0.5f * (float) this->options->simHeight;
        float norm = std::sqrt(xf * xf + yf * yf);
        float vx = (norm < 1e-5) ? 0.0f : - 200.0f * yf / norm;
        float vy = (norm < 1e-5) ? 0.0f :   200.0f * xf / norm;
        return std::make_tuple(50.0f, 0.0f,
                               0.0f, 0.0f);
      };

  density[0] = createTexture2D(options->simWidth, options->simHeight);
  density[1] = createTexture2D(options->simWidth, options->simHeight);
  density[2] = createTexture2D(options->simWidth, options->simHeight);
  density[3] = createTexture2D(options->simWidth, options->simHeight);
  fillTextureWithFunctor(density[0], options->simWidth, options->simHeight, f);

  temperature[0] = createTexture2D(options->simWidth, options->simHeight);
  temperature[1] = createTexture2D(options->simWidth, options->simHeight);
  temperature[2] = createTexture2D(options->simWidth, options->simHeight);
  temperature[3] = createTexture2D(options->simWidth, options->simHeight);
  fillTextureWithFunctor(temperature[0], options->simWidth, options->simHeight, f);

  velocitiesTexture[0] = createTexture2D(options->simWidth, options->simHeight);
  velocitiesTexture[1] = createTexture2D(options->simWidth, options->simHeight);
  velocitiesTexture[2] = createTexture2D(options->simWidth, options->simHeight);
  velocitiesTexture[3] = createTexture2D(options->simWidth, options->simHeight);
  fillTextureWithFunctor(velocitiesTexture[0], options->simWidth, options->simHeight, f);

  divergenceCurlTexture = createTexture2D(options->simWidth, options->simHeight);
  fillTextureWithFunctor(divergenceCurlTexture, options->simWidth, options->simHeight, f);

  pressureTexture[0] = createTexture2D(options->simWidth, options->simHeight);
  pressureTexture[1] = createTexture2D(options->simWidth, options->simHeight);
  fillTextureWithFunctor(pressureTexture[0], options->simWidth, options->simHeight, f);

  emptyTexture = createTexture2D(options->simWidth, options->simHeight);
  fillTextureWithFunctor(emptyTexture, options->simWidth, options->simHeight,
      [](int x, int y)
      {
        return std::make_tuple(0.0f, 0.0f, 0.0f, 0.0f);
      });
}

void Smoke::SetHandler(GLFWHandler* hand)
{
  handler = hand;
}

void Smoke::AddSplat()
{
}

void Smoke::AddMultipleSplat(const int nb)
{
}

void Smoke::RemoveSplat()
{
}

void Smoke::Update()
{
  GLint64 startTime, stopTime;
  GLuint queryID[2];

  // generate two queries
  glGenQueries(2, queryID);
  glQueryCounter(queryID[0], GL_TIMESTAMP); 

  /********** Adding Smoke Origin *********/
  auto rd = []() -> double
  {
    return (double) rand() / (double) RAND_MAX;
  };

  int x = options->simWidth / 2; int y = 75;

  sFact.addSplat(density[READ],           std::make_tuple(x, y), std::make_tuple(0.12f, 0.31f, 0.7f), 1.0f);
  sFact.addSplat(temperature[READ],       std::make_tuple(x, y), std::make_tuple(rd() * 20.0f + 10.0f, 0.0f, 0.0f), 8.0f);
  sFact.addSplat(velocitiesTexture[READ], std::make_tuple(x, y), std::make_tuple(2.0f * rd() - 1.0f, 0.0f, 0.0f), 75.0f);

  /********** Divergence & Curl **********/
  sFact.divergenceCurl(velocitiesTexture[READ], divergenceCurlTexture);

  /********** Vorticity **********/
  sFact.applyVorticity(velocitiesTexture[READ], divergenceCurlTexture, options->dt);

  /********** Buoyant Force **********/
  sFact.applyBuoyantForce(velocitiesTexture[READ], temperature[READ], density[READ], options->dt, 0.25f, 0.1f, 15.0f);

  /********** Convection **********/
  sFact.mcAdvect(velocitiesTexture[READ], velocitiesTexture, options->dt);
  std::swap(velocitiesTexture[0], velocitiesTexture[3]);

  /********** Poisson Solving with Jacobi **********/
  sFact.copy(emptyTexture, pressureTexture[READ]);
  for(int k = 0; k < 25; ++k)
  {
    sFact.solvePressure(divergenceCurlTexture, pressureTexture[READ], pressureTexture[WRITE]);
    std::swap(pressureTexture[READ], pressureTexture[WRITE]);
  }

  /********** Pressure Projection **********/
  sFact.pressureProjection(pressureTexture[READ], velocitiesTexture[READ], velocitiesTexture[WRITE]);
  std::swap(velocitiesTexture[READ], velocitiesTexture[WRITE]);

  /********** Fields Advection **********/
  sFact.mcAdvect(velocitiesTexture[READ], density, options->dt);
  std::swap(density[0], density[3]);

  sFact.mcAdvect(velocitiesTexture[READ], temperature, options->dt);
  std::swap(temperature[0], temperature[3]);

  /********** Updating the shared texture **********/
  shared_texture = density[READ];

  /********** Time Stuff **********/
  glQueryCounter(queryID[1], GL_TIMESTAMP);
  GLint stopTimerAvailable = 0;
  while (!stopTimerAvailable) {
      glGetQueryObjectiv(queryID[1],
                            GL_QUERY_RESULT_AVAILABLE,
                            &stopTimerAvailable);
  }
  glGetQueryObjectui64v(queryID[0], GL_QUERY_RESULT, (GLuint64*) &startTime);
  glGetQueryObjectui64v(queryID[1], GL_QUERY_RESULT, (GLuint64*) &stopTime);
  
  printf("\r%.3f FPS", 1000.0 / ((stopTime - startTime) / 1000000.0));
  fflush(stdout);
}
