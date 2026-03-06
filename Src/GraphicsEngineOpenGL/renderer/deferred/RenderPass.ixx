export module kataglyphis.opengl.render_pass;

export class RenderPass
{
  public:
    virtual ~RenderPass() = default;
    virtual void create_shader_program() = 0;

  private:
};
