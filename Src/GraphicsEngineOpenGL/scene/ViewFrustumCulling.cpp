#include "scene/ViewFrustumCulling.hpp"

ViewFrustumCulling::ViewFrustumCulling()
  :

    VBO(-1), VAO(-1), EBO(-1), m_drawCount(0),
    // we get that as input
    near_plane(0.f), far_plane(0.f), fov(0.f), ratio(0.f),

    // calculate as soon as we become params
    tan(0.f), near_height(0.f), near_width(0.f), far_height(0.f), far_width(0.f), main_camera(),

    dir(glm::vec3(0.f)), near_center(glm::vec3(0.f)), far_center(glm::vec3(0.f)),

    near_top_left(glm::vec3(0.f)), near_top_right(glm::vec3(0.f)), near_bottom_left(glm::vec3(0.f)),
    near_bottom_right(glm::vec3(0.f)),

    far_top_left(glm::vec3(0.f)), far_top_right(glm::vec3(0.f)), far_bottom_left(glm::vec3(0.f)),
    far_bottom_right(glm::vec3(0.f))

{}

bool ViewFrustumCulling::is_inside(GLfloat ratio,
  std::shared_ptr<Camera> main_camera,
  std::shared_ptr<AABB> bounding_box,
  glm::mat4 model)
{
    GLfloat near_plane = main_camera->get_near_plane();
    GLfloat far_plane = main_camera->get_far_plane();
    GLfloat fov = main_camera->get_fov();

    update_frustum_param(near_plane, far_plane, fov, ratio, main_camera);

    std::vector<glm::vec3> aabb_corners = bounding_box->get_corners(model);

    // layout:                      [0]: near plane, [1] far plane, [2] up    ,
    // [3] bottom , [4]: left , [5]: right outcodes (binary) :  100000 , 010000 ,
    // 000100 , 001000     , 000010, 000001 outcodes (dezi)     :  32 , 16 , 4 , 8
    // , 2           , 1
    bool result = true;

    GLint outcode_near_plane = 32;
    GLint outcode_far_plane = 16;
    GLint outcode_up = 4;
    GLint outcode_bottom = 8;
    GLint outcode_left = 2;
    GLint outcode_right = 1;
    // GLint outcode;

    GLint outcodes_pattern[NUM_FRUSTUM_PLANES] = {
        outcode_near_plane, outcode_far_plane, outcode_up, outcode_bottom, outcode_left, outcode_right
    };

    for (int i = 0; i < NUM_FRUSTUM_PLANES; i++) {
        frustum_plane plane = frustum_planes[i];

        if (corners_outside_plane(aabb_corners, plane, outcodes_pattern[i])) {
            result = false;
            break;
        }
    }

    return result;
}

void ViewFrustumCulling::render_view_frustum()
{
    // seeing as we only have a single VAO there's no need to bind it every time,
    // but we'll do so to keep things a bit more organized
    glBindVertexArray(VAO);
    // glDrawArrays(GL_TRIANGLES, 0, 6);
    glDrawElements(GL_TRIANGLES, m_drawCount, GL_UNSIGNED_INT, 0);

    // unbind all again
    glBindVertexArray(0);
}

bool ViewFrustumCulling::corners_outside_plane(std::vector<glm::vec3> aabb_corners,
  frustum_plane plane,
  GLuint outcode_pattern)
{
    GLint outcode = outcode_pattern;

    for (int i = 0; i < static_cast<int>(aabb_corners.size()); i++) {
        if (plane_point_distance(plane, aabb_corners[i]) < 0.0f) {
            if (i == 0) {
                outcode = outcode_pattern;
            } else {
                outcode = outcode & outcode_pattern;
            }
        } else {
            if (i == 0) {
                outcode = 0;
            } else {
                outcode = outcode & 0;
            }
        }
    }

    return (outcode != 0) ? true : false;
}

GLfloat ViewFrustumCulling::plane_point_distance(frustum_plane plane, glm::vec3 corner)
{
    GLfloat result = 0.0f;

    glm::vec3 plane_normal = plane.normal;
    glm::vec3 plane_position = plane.position;

    GLfloat d = glm::dot(plane_normal, plane_position);

    result = (glm::dot(plane_normal, corner) - d) / glm::length(plane_normal);

    return result;
}

void ViewFrustumCulling::update_frustum_param(GLfloat near_plane,
  GLfloat far_plane,
  GLfloat fov,
  GLfloat ratio,
  std::shared_ptr<Camera> main_camera)
{
    this->near_plane = near_plane;
    this->far_plane = far_plane;
    this->fov = fov;
    this->ratio = ratio;

    tan = glm::tan(glm::radians(fov) * 0.5f);
    near_height = near_plane * tan;
    near_width = near_height * ratio;
    far_height = far_plane * tan;
    far_width = far_height * ratio;

    this->main_camera = main_camera;

    near_center = main_camera->get_camera_position() + main_camera->get_camera_direction() * near_plane;
    far_center = main_camera->get_camera_position() + main_camera->get_camera_direction() * far_plane;

    glm::vec3 aux_position, aux, aux_normal;

    // layout:  [0]: near plane
    frustum_planes[0].normal = main_camera->get_camera_direction();
    frustum_planes[0].position = near_center;

    // [1] far plane
    frustum_planes[1].normal = -main_camera->get_camera_direction();
    frustum_planes[1].position = far_center;

    aux_position = near_center + main_camera->get_up_axis() * near_height;
    aux = aux_position - main_camera->get_camera_position();
    aux = glm::normalize(aux);
    aux_normal = glm::cross(aux, main_camera->get_right_axis());

    // [2] top
    frustum_planes[2].normal = normalize(aux_normal);
    frustum_planes[2].position = aux_position;

    aux_position = near_center - main_camera->get_up_axis() * near_height;
    aux = aux_position - main_camera->get_camera_position();
    aux = glm::normalize(aux);
    aux_normal = glm::cross(main_camera->get_right_axis(), aux);

    // [3] bottom
    frustum_planes[3].normal = normalize(aux_normal);
    frustum_planes[3].position = aux_position;

    aux_position = near_center - main_camera->get_right_axis() * near_width;
    aux = aux_position - main_camera->get_camera_position();
    aux = glm::normalize(aux);
    aux_normal = glm::cross(aux, main_camera->get_up_axis());

    // [4]: left
    frustum_planes[4].normal = normalize(aux_normal);
    frustum_planes[4].position = aux_position;

    aux_position = near_center + main_camera->get_right_axis() * near_width;
    aux = aux_position - main_camera->get_camera_position();
    aux = glm::normalize(aux);
    aux_normal = glm::cross(main_camera->get_up_axis(), aux);

    // [5]: right
    frustum_planes[5].normal = normalize(aux_normal);
    frustum_planes[5].position = aux_position;

    std::vector<glm::vec3> frustum_corners;

    // frustum_corners.push_back(near_center - main_camera->get_right_axis() *
    // near_width -
    //  main_camera->get_up_axis() * near_height);// left bottom front
    //
    // frustum_corners.push_back(far_center - main_camera->get_right_axis() *
    // far_width -
    //  main_camera->get_up_axis() * far_height);// left bottom back
    //
    // frustum_corners.push_back(near_center - main_camera->get_right_axis() *
    // near_width +
    //   main_camera->get_up_axis() * near_height); // left top front
    //
    // frustum_corners.push_back(far_center - main_camera->get_right_axis() *
    // far_width +
    //   main_camera->get_up_axis() * far_height);// left top back
    //
    // frustum_corners.push_back(near_center + main_camera->get_right_axis() *
    // near_width -
    //  main_camera->get_up_axis() * near_height); // right bottom front
    //
    // frustum_corners.push_back(far_center + main_camera->get_right_axis() *
    // far_width -
    //  main_camera->get_up_axis() * far_height);//right bottom back
    //
    // frustum_corners.push_back(near_center + main_camera->get_right_axis() *
    // near_width +
    //  main_camera->get_up_axis() * near_height);//right top front
    //
    // frustum_corners.push_back(far_center + main_camera->get_right_axis() *
    // far_width + main_camera->get_up_axis() * far_height);//right top back

    // init(frustum_corners);
}

void ViewFrustumCulling::init(std::vector<glm::vec3> frustum_corner)
{
    // unsigned int num_corners = 8;
    m_drawCount = 36;// num_corners;

    float vertices[] = {

        frustum_corner[0].x,
        frustum_corner[0].y,
        frustum_corner[0].z,// left bottom front
        frustum_corner[1].x,
        frustum_corner[1].y,
        frustum_corner[1].z,// left bottom back
        frustum_corner[2].x,
        frustum_corner[2].y,
        frustum_corner[2].z,// left top front
        frustum_corner[3].x,
        frustum_corner[3].y,
        frustum_corner[3].z,// left top back
        frustum_corner[4].x,
        frustum_corner[4].y,
        frustum_corner[4].z,// right bottom front
        frustum_corner[5].x,
        frustum_corner[5].y,
        frustum_corner[5].z,// right bottom back
        frustum_corner[6].x,
        frustum_corner[6].y,
        frustum_corner[6].z,// right top front
        frustum_corner[7].x,
        frustum_corner[7].y,
        frustum_corner[7].z// right top back

    };

    unsigned int indices[] = {
        // note that we start from 0!
        // left
        0,
        1,
        3,
        0,
        2,
        3,
        // right
        4,
        5,
        7,
        4,
        6,
        7,
        // top
        3,
        2,
        7,
        3,
        2,
        8,
        // bottom
        0,
        1,
        4,
        0,
        1,
        5,
        // back
        1,
        3,
        5,
        1,
        3,
        7,
        // front
        0,
        2,
        4,
        0,
        2,
        6,
    };

    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);
    glGenBuffers(1, &EBO);
    // bind the Vertex Array Object first, then bind and set vertex buffer(s), and
    // then configure vertex attributes(s).
    glBindVertexArray(VAO);

    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void *)0);
    glEnableVertexAttribArray(0);

    // note that this is allowed, the call to glVertexAttribPointer registered VBO
    // as the vertex attribute's bound vertex buffer object so afterwards we can
    // safely unbind
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    // remember: do NOT unbind the EBO while a VAO is active as the bound element
    // buffer object IS stored in the VAO; keep the EBO bound.
    // glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

    // You can unbind the VAO afterwards so other VAO calls won't accidentally
    // modify this VAO, but this rarely happens. Modifying other VAOs requires a
    // call to glBindVertexArray anyways so we generally don't unbind VAOs (nor
    // VBOs) when it's not directly necessary.
    glBindVertexArray(0);
}

ViewFrustumCulling::~ViewFrustumCulling()
{
    glDeleteVertexArrays(1, &VAO);
    glDeleteBuffers(1, &VBO);
    glDeleteBuffers(1, &EBO);
}
